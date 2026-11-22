/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Authors: Simon Kuenzer <simon@unikraft.io>
 *
 * Copyright (c) 2019, NEC Laboratories Europe GmbH,
 *                     NEC Corporation. All rights reserved.
 * Copyright (c) 2023, Unikraft GmbH. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <uk/config.h>
#include <libelf.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <uk/errptr.h>
#include <uk/essentials.h>
#include <uk/plat/memory.h>
#if CONFIG_LIBPOSIX_PROCESS
#include <uk/process.h>
#endif /* CONFIG_LIBPOSIX_PROCESS */
#include <uk/thread.h>
#include <uk/sched.h>
#if CONFIG_LIBUKRANDOM
#include <uk/random.h>
#endif /* CONFIG_LIBUKRANDOM */
#if CONFIG_APPELFLOADER_VFSEXEC_ENVPATH
#include <uk/argparse.h>
#include <uk/streambuf.h>
#endif /* CONFIG_APPELFLOADER_VFSEXEC_ENVPATH */

#include "elf_prog.h"
#if CONFIG_APPELFLOADER_PROCESS_SESSION_TEST
#include "elf_session.h"
#endif
#if CONFIG_APPELFLOADER_PROCESS_VAS
#include <errno.h>
#include <sys/wait.h>
#include <uk/crash.h>
#include <uk/process/prepared.h>
#include <uk/syscall.h>
#include <uk/vmem/process.h>
#include <uk/vmem/vma_types.h>
#endif


#if CONFIG_LIBPOSIX_ENVIRON
extern const char **environ;
#else /* !CONFIG_LIBPOSIX_ENVIRON */
#define environ NULL
#endif /* !CONFIG_LIBPOSIX_ENVIRON */

#ifndef PAGES2BYTES
#define PAGES2BYTES(x) ((x) << __PAGE_SHIFT)
#endif

/*
 * Internal version of `basename`
 * We keep an own version here that modifies the input string in-place.
 * Main reasons are that `nolibc` does not provide `basename()` and there exist
 * two versions, in general: a GNU variant with `<string.h>` and a POSIX variant
 * with `<libgen.h>`. Depending on the libc used, only one or the other could be
 * available.
 * NOTE: This version modifies the input string by overwriting trailing slashes.
 */
static inline char *basename_internal(char *path)
{
	char *bn;

	if (unlikely(!path))
		return NULL;

again:
	bn = strrchr(path, '/');
	if (!bn) {
		/* No slash found, path is basename */
		return path;
	}
	if (bn[1] == '\0') {
		/* Remove trailing slash */
		bn[0] = '\0';
		goto again;
	}
	return ++bn;
}

#if CONFIG_APPELFLOADER_VFSEXEC_ENVPATH
/*
 * Routine that locates an executable in a colon-separated list of directories.
 * On success, it returns a malloc'ed C-string containing the full path. It is
 * in the responsibility of the caller to free the string after use with
 * `free()`.
 */
static inline char *locate_exec(const char *basename, const char *path_env)
{
	struct uk_streambuf sb;
	const char *path_next;
	const char *path_cur;
	size_t path_cur_len;
	struct stat f_stat;
	char *buf;
	int err;

	if (!basename || basename[0] == '/' || basename[0] == '.') {
		/* no name given, absolute, or cwd-relative */
		err = -EINVAL;
		goto err_out;
	}

	buf = malloc(PATH_MAX);
	if (!buf) {
		err = -ENOMEM;
		goto err_out;
	}

	/* Iterate over paths */
	path_cur  = path_env;
	path_next = path_env;
	while (path_next) {
		path_cur     = path_next;
		path_cur_len = uk_nextarg_r(&path_next, ':');

		uk_streambuf_init(&sb, buf, PATH_MAX, UK_STREAMBUF_C_TERMSHIFT);
		uk_streambuf_memcpy(&sb, path_cur, path_cur_len);
		if (path_cur_len > 0)
			uk_streambuf_reserve(&sb, 2); /* last byte of memcpy
						       * and 1 byte of reserve
						       * would be overwritten by
						       * successive strcpy calls
						       */
		uk_streambuf_strcpy(&sb, "/");
		uk_streambuf_strcpy(&sb, basename);
		if (uk_streambuf_istruncated(&sb)) {
			err = -ENOSPC;
			goto err_free_buf;
		}
		uk_pr_debug("Looking for executable under %s...\n",
			    (char *)uk_streambuf_buf(&sb));
		if (stat((char *)uk_streambuf_buf(&sb), &f_stat) != 0)
			continue; /* file not found */
		if (unlikely(!(f_stat.st_mode & S_IFREG)))
			continue; /* found but not a file */
#if CONFIG_APPELFLOADER_VFSEXEC_EXECBIT
		if (unlikely(!(f_stat.st_mode & (S_IXUSR | S_IFREG))))
			continue; /* found but not an executable */
#endif /* !CONFIG_APPELFLOADER_VFSEXEC_EXECBIT */

		uk_pr_debug("+ Found.\n");
		return buf;
	}

	uk_pr_debug("No executable found for %s\n", basename);
	err = -ENOENT;
err_free_buf:
	free(buf);
err_out:
	return ERR2PTR(err);
}
#endif /* CONFIG_APPELFLOADER_VFSEXEC_ENVPATH */

/*
 * Init libelf
 */
static __constructor void _libelf_init(void) {
	if (elf_version(EV_CURRENT) == EV_NONE)
		UK_CRASH("Failed to initialize libelf: Version error");
}

#if CONFIG_APPELFLOADER_PROCESS_VAS
static int elf_process_stack(struct uk_vas *vas, __sz len, __vaddr_t *top)
{
	__vaddr_t base;
	int rc;

	if (!len || len > vas->range_end - vas->range_start -
	    2 * UK_PAGING_PAGE_SIZE)
		return -E2BIG;
	base = vas->range_end - len - 2 * UK_PAGING_PAGE_SIZE;
	rc = uk_vma_map_anon(vas, &base, len + 2 * UK_PAGING_PAGE_SIZE,
			     UK_PAGING_PAGE_ATTR_PROT_NONE, 0, "user stack");
	if (rc)
		return rc;
	rc = uk_vma_set_attr(vas, base + UK_PAGING_PAGE_SIZE, len,
			     UK_PAGING_PAGE_ATTR_PROT_RW, 0);
	if (!rc)
		*top = base + UK_PAGING_PAGE_SIZE + len;
	return rc;
}

static int elf_process_run(const char *name, const char *path __unused,
			   void *image __unused, size_t image_len __unused,
			   const char *argv[])
{
	struct uk_sched *sched = uk_sched_current();
	struct uk_thread *current = uk_thread_current(), *child = NULL;
	struct uk_vas *saved = current->vas, *vas = NULL;
	struct elf_prog *prog = NULL;
	const __sz stack_len = (__sz)CONFIG_APPELFLOADER_STACK_NBPAGES *
		UK_PAGING_PAGE_SIZE;
	__vaddr_t top;
	__u64 random[2];
	pid_t pid = -1, waited;
	bool bound = false;
	int argc, envc, status, rc;
#if CONFIG_APPELFLOADER_VFSEXEC_ENVPWD
	const char *pwd = getenv("PWD");
#endif

	if (uk_vas_get_active() != uk_vas_get_kernel()) {
		rc = -EBUSY;
		goto error;
	}
#if CONFIG_APPELFLOADER_VFSEXEC_ENVPWD
	if (pwd && chdir(pwd)) {
		rc = -errno;
		goto error;
	}
#endif
	/* The user stack belongs to the VAS, independently of the container. */
	child = uk_thread_create_container(sched->a, NULL, 0,
					   sched->a_auxstack, 0, sched->a_uktls,
					   false, "elf-application",
					   NULL, NULL);
	if (!child) {
		rc = -ENOMEM;
		goto error;
	}
	pid = uk_posix_process_prepare_spawn(sched->a, child, &vas);
	if (pid < 0) {
		rc = pid;
		goto error;
	}
	rc = uk_thread_set_vas(current, vas);
	if (rc)
		goto error;
	bound = true;
#if CONFIG_APPELFLOADER_INITRDEXEC
	prog = elf_load_img(uk_alloc_get_default(), image, image_len, name);
#else
	prog = elf_load_vfs(uk_alloc_get_default(), path, name);
#endif
	if (!prog || PTRISERR(prog)) {
		rc = prog ? PTR2ERR(prog) : -ENOEXEC;
		prog = NULL;
		goto error;
	}
	rc = elf_ctx_check_args(prog, name, &argc, argv, &envc, environ,
				stack_len);
	if (!rc)
		rc = elf_process_stack(vas, stack_len, &top);
	if (rc)
		goto error;
#if CONFIG_LIBUKRANDOM
	rc = uk_random_fill_buffer(random, sizeof(random));
	if (rc)
		goto error;
#else
	random[0] = 0xB0B0;
	random[1] = 0xF00D;
#endif
	child->ctx.sp = top;
	elf_ctx_init(&child->ctx, prog, name, argc, argv, envc, environ,
		     random);
	elf_release_metadata(prog);
	prog = NULL;
	if (uk_thread_set_vas(current, saved))
		UK_CRASH("Cannot restore loader address space\n");
	bound = false;
#ifdef UK_POSIX_PROCESS_HAS_PROGRAM_STACK_SIZE
	uk_posix_process_set_stack_size(child, stack_len);
#endif /* UK_POSIX_PROCESS_HAS_PROGRAM_STACK_SIZE */
	rc = uk_sched_thread_add(sched, child);
	if (rc)
		goto error;
	rc = uk_posix_process_publish_prepared(child);
	if (rc) {
		if (uk_sched_thread_remove(child))
			UK_CRASH("Cannot withdraw prepared application\n");
		goto error;
	}
	/* Process and thread pins retain the VAS through execution and exec. */
	uk_vas_put(vas);
	vas = NULL;
	uk_thread_wake(child);
	child = NULL;
	do {
		waited = waitpid(pid, &status, 0);
	} while (waited < 0 && errno == EINTR);
	if (waited != pid)
		return 1;
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
error:
	if (prog)
		elf_release_metadata(prog);
	if (bound && uk_thread_set_vas(current, saved))
		UK_CRASH("Cannot restore failed loader address space\n");
	if (pid > 0 && uk_posix_process_abort_prepared(child))
		UK_CRASH("Cannot abort prepared application\n");
	if (child)
		uk_thread_release(child);
	uk_vas_put(vas);
	uk_pr_err("Cannot start %s (%d)\n", name, rc);
	return 1;
}

#if CONFIG_APPELFLOADER_PROCESS_SESSION_TEST
struct elf_session_args {
	const char *name;
	const char *path;
	void *image;
	size_t image_len;
	const char **argv;
};

static int elf_process_session_run(void *arg)
{
	struct elf_session_args *args = arg;

	return elf_process_run(args->name, args->path, args->image,
			       args->image_len, args->argv);
}

static int elf_process_entry(const char *name, const char *path, void *image,
			     size_t image_len, const char *argv[])
{
	struct elf_session_args args = {
		.name = name, .path = path, .image = image,
		.image_len = image_len, .argv = argv
	};

	return elf_session_observe(elf_process_session_run, &args);
}
#else
#define elf_process_entry elf_process_run
#endif
#endif /* CONFIG_APPELFLOADER_PROCESS_VAS */

int main(int argc, const char *argv[])
{
#if CONFIG_APPELFLOADER_INITRDEXEC
	struct ukplat_memregion_desc *img;
	int rc;
#else /* CONFIG_APPELFLOADER_VFSEXEC */
	const char *path;
	char *realpath = NULL;
	/* reference of strdup()'ed `path` that is converted into `progname` */
	char *progname_conv = NULL;
#endif /* CONFIG_APPELFLOADER_VFSEXEC */
	const char *progname;
	struct elf_prog *prog;
	struct uk_thread *app_thread;
	struct uk_sched *s = uk_sched_current();
	const __sz stack_size = PAGES2BYTES(CONFIG_APPELFLOADER_STACK_NBPAGES);
	uint64_t rand[2];
	int ret = 0;
#if CONFIG_APPELFLOADER_VFSEXEC_ENVPATH
	char *env_path;
#endif /* CONFIG_APPELFLOADER_VFSEXEC_ENVPATH */
#if CONFIG_APPELFLOADER_VFSEXEC_ENVPWD
	char *env_pwd;
#endif /* CONFIG_APPELFLOADER_VFSEXEC_ENVPWD */
	int envc;

	UK_ASSERT(s);

	/*
	 * Prepare `progname` (and `path`) from command line
	 * or compiled-in settings
	 */
#if CONFIG_APPELFLOADER_CUSTOMAPPNAME
	if (unlikely(argc <= 1 || !argv)) {
		uk_pr_err("Program name missing (no argv[1])\n");
		ret = 1;
		goto out;
	}
#if CONFIG_APPELFLOADER_INITRDEXEC
	progname = argv[1];
#else /* CONFIG_APPELFLOADER_VFSEXEC */
	path          = argv[1];
	/* retrieve progname from path */
	progname_conv = strdup(path);
	progname      = basename_internal(progname_conv);
#endif /* CONFIG_APPELFLOADER_VFSEXEC */

#if CONFIG_LIBPOSIX_PROCESS_EXECVE && CONFIG_APPELFLOADER_VFSEXEC && \
	!CONFIG_APPELFLOADER_PROCESS_VAS
	/* Cut off kernel name (argv[0]) from argument vector */
	argv = &argv[1];
	argc -= 1;
#else /* !(CONFIG_LIBPOSIX_PROCESS_EXECVE && CONFIG_APPELFLOADER_VFSEXEC) */
	/* Cut off kernel name (argv[0]) and program name (argv[1])
	 * from argument vector
	 */
	argv = &argv[2];
	argc -= 2;
#endif /* !(CONFIG_LIBPOSIX_PROCESS_EXECVE && CONFIG_APPELFLOADER_VFSEXEC) */

#else /* !CONFIG_APPELFLOADER_CUSTOMAPPNAME */
	/* Ensure argv[0] exists and is set
	 * NOTE: Because we can assume to have argv[0] always set by convention,
	 *       we use an assertion here.
	 */
	UK_ASSERT(argc >= 1 && argv && argv[0]);

#if CONFIG_APPELFLOADER_INITRDEXEC
	progname = argv[0];
#else /* CONFIG_APPELFLOADER_VFSEXEC */
	path          = CONFIG_APPELFLOADER_VFSEXEC_PATH;
	/* retrieve progname from path */
	progname_conv = strdup(path);
	progname      = basename_internal(progname_conv);
#endif /* CONFIG_APPELFLOADER_VFSEXEC */

	/* Cut off kernel name (argv[0]) from argument vector */
	argv = &argv[1];
	argc -= 1;

#endif /* !CONFIG_APPELFLOADER_CUSTOMAPPNAME */

#if CONFIG_APPELFLOADER_VFSEXEC_ENVPATH
	env_path = getenv("PATH");
	if (env_path) {
		realpath = locate_exec(path, env_path);
		if (PTR2ERR(realpath) == -EINVAL) {
			realpath = NULL;
		} else if (PTRISERR(realpath) && PTR2ERR(realpath) != -EINVAL) {
			uk_pr_err("%s: Failed to find executable in environment ($PATH): %s (%d)\n",
				  progname, strerror(-PTR2ERR(realpath)),
				  PTR2ERR(realpath));
			ret = PTR2ERR(realpath);
			goto out;
		}
	}
#endif /* CONFIG_APPELFLOADER_VFSEXEC_ENVPATH */

#if CONFIG_LIBPOSIX_PROCESS_EXECVE && CONFIG_APPELFLOADER_VFSEXEC && \
	!CONFIG_APPELFLOADER_PROCESS_VAS
	ret = execve(realpath ? realpath : path,
		     (char * const *)argv, (char * const *)environ);
	if (unlikely(ret)) {
		uk_pr_err("Could not execve (%d)\n", ret);
		goto out;
	}
#endif /* CONFIG_LIBPOSIX_PROCESS_EXECVE && CONFIG_APPELFLOADER_VFSEXEC */

#if CONFIG_APPELFLOADER_INITRDEXEC
	/*
	 * Locate ELF initramdisk
	 */
	uk_pr_debug("Searching for ELF initramdisk...\n");
	rc = ukplat_memregion_find_initrd0(&img);
	if (unlikely(rc < 0 || !img->vbase || !img->len)) {
		uk_pr_err("No image found (initrd parameter missing?)\n");
		ret = 1;
		goto out;
	}
	uk_pr_info("Image at %p, len %"__PRIsz" bytes\n",
		   (void *) img->vbase, img->len);
#endif /* CONFIG_APPELFLOADER_INITRDEXEC */

#if CONFIG_APPELFLOADER_PROCESS_VAS
#if CONFIG_APPELFLOADER_INITRDEXEC
	ret = elf_process_entry(progname, NULL, (void *)img->vbase, img->len,
				argv);
#else
	ret = elf_process_entry(progname, realpath ? realpath : path, NULL, 0,
				argv);
#endif
	goto out;
#endif

	/*
	 * Create thread container
	 * It will have a new stack and an ukarch_ctx
	 */
	app_thread = uk_thread_create_container(uk_alloc_get_default(),
						s->a_stack,
				 PAGES2BYTES(CONFIG_APPELFLOADER_STACK_NBPAGES),
						s->a_auxstack,
						0,
						s->a_uktls,
						false,
						progname,
						NULL, NULL);
	if (unlikely(!app_thread)) {
		uk_pr_err("%s: Failed to allocate thread container\n",
			  progname);
		ret = 1;
		goto out;
	}

#if CONFIG_APPELFLOADER_VFSEXEC_ENVPWD
	/*
	 * Set working directory if `PWD` env variable is set
	 * FIXME: Remotely set this for target thread
	 */
	env_pwd = getenv("PWD");
	if (env_pwd) {
		uk_pr_debug("%s: Changing working directory to '%s'\n",
			    progname, env_pwd);
		if (chdir(env_pwd) < 0) {
			uk_pr_err("%s: Failed to change working directory to '%s': %s (%d)\n",
				  progname, env_pwd, strerror(errno), errno);
			goto out_free_thread;
		}
	}
#endif /* CONFIG_APPELFLOADER_VFSEXEC_ENVPWD */

	/*
	 * Parse image
	 */
#if CONFIG_APPELFLOADER_INITRDEXEC
	uk_pr_debug("%s: Load executable...\n", progname);
	prog = elf_load_img(uk_alloc_get_default(), (void *) img->vbase,
			    img->len, progname);
#else /* CONFIG_APPELFLOADER_VFSEXEC */
	uk_pr_debug("%s: Load executable (%s)...\n",
		    progname, realpath ? realpath : path);
	prog = elf_load_vfs(uk_alloc_get_default(),
			    realpath ? realpath : path, progname);

#endif /* CONFIG_APPELFLOADER_VFSEXEC */
	if (unlikely(PTRISERR(prog) || !prog)) {
		ret = -errno;
		goto out_free_thread;
	}
	uk_pr_info("%s: ELF program loaded to 0x%"PRIx64"-0x%"PRIx64" (%"__PRIsz" B), entry at %p\n",
		   progname,
		   (uint64_t) prog->vabase,
		   (uint64_t) prog->vabase + prog->valen,
		   prog->valen, (void *) prog->entry);

	/*
	 * Initialize application thread
	 */
#if CONFIG_LIBUKRANDOM
	ret = uk_random_fill_buffer(rand, sizeof(rand));
	if (unlikely(ret)) {
		uk_pr_err("Could not get random bytes (%d)\n", ret);
		goto out_free_thread;
	}
#else /* !CONFIG_LIBUKRANDOM */
	/* Without random numbers, use a hardcoded seed */
	uk_pr_warn("%s: Using hard-coded random seed\n", progname);
	rand[0] = 0xB0B0;
	rand[1] = 0xF00D;
#endif /* !CONFIG_LIBUKRANDOM */

	uk_pr_debug("%s: Prepare application thread...\n", progname);
	ret = elf_ctx_check_args(prog, progname, &argc, argv, &envc,
				 environ, stack_size);
	if (unlikely(ret < 0)) {
		uk_pr_err("Args + env size exceeds limit, increase stack size\n");
		goto out_free_thread;
	}

	elf_ctx_init(&app_thread->ctx, prog, progname,
		     argc, argv, envc, environ, rand);
	app_thread->flags |= UK_THREADF_RUNNABLE;

#if CONFIG_LIBPOSIX_PROCESS_MULTITHREADING
	/* Add application thread to process */
	ret = uk_posix_process_create_pthread(app_thread);
	if (unlikely(ret)) {
		uk_pr_err("Could not create pthread\n");
		goto out_free_thread;
	}
#ifdef UK_POSIX_PROCESS_HAS_PROGRAM_STACK_SIZE
	uk_posix_process_set_stack_size(app_thread, stack_size);
#endif /* UK_POSIX_PROCESS_HAS_PROGRAM_STACK_SIZE */
#endif /* CONFIG_LIBPOSIX_PROCESS_MULTITHREADING */

	uk_pr_debug("%s: Application stack at %p - %p, pointer: %p\n",
		    progname,
		    (void *) app_thread->_mem.stack,
		    (void *) ((uintptr_t) app_thread->_mem.stack
			      + PAGES2BYTES(CONFIG_APPELFLOADER_STACK_NBPAGES)),
		    (void *) app_thread->ctx.sp);
	uk_pr_debug("%s: Application entry at %p\n",
		    progname,
		    (void *) app_thread->ctx.ip);

	/*
	 * Execute application
	 */
	uk_sched_thread_add(s, app_thread);

	/*
	 * FIXME: Instead of an infinite wait, wait for application
	 *        to exit (this needs thread_wait support with
	 *        uksched and/or posix-process)
	 */
	for (;;)
		sleep(10);

out_free_thread:
	uk_thread_release(app_thread);
out:
#if CONFIG_APPELFLOADER_VFSEXEC
#if CONFIG_APPELFLOADER_VFSEXEC_ENVPATH
	if (realpath && !PTRISERR(realpath))
		free(realpath);
#endif /* CONFIG_APPELFLOADER_VFSEXEC_ENVPATH */
	if (progname_conv && !PTRISERR(progname_conv))
		free(progname_conv);
#endif /* CONFIG_APPELFLOADER_VFSEXEC */
#if CONFIG_APPELFLOADER_PROCESS_VAS && CONFIG_LIBUKBOOT_INIT
	/* Let init reap descendants before deciding when to shut down. */
	uk_syscall_r(SYS_exit_group, (long)ret);
	UK_CRASH("Loader process termination unexpectedly returned\n");
#endif
	return ret;
}
