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
#if CONFIG_LIBUKSWRAND
#include <uk/swrand.h>
#endif /* CONFIG_LIBUKSWRAND */
#if CONFIG_APPELFLOADER_VFSEXEC_ENVPATH
#include <uk/argparse.h>
#include <uk/streambuf.h>
#endif /* CONFIG_APPELFLOADER_VFSEXEC_ENVPATH */

#include "elf_prog.h"

#if CONFIG_LIBPOSIX_ENVIRON
extern char **environ;
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

int main(int argc, char *argv[])
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
	uint64_t rand[2];
	int ret = 0;
#if CONFIG_APPELFLOADER_VFSEXEC_ENVPATH
	char *env_path;
#endif /* CONFIG_APPELFLOADER_VFSEXEC_ENVPATH */
#if CONFIG_APPELFLOADER_VFSEXEC_ENVPWD
	char *env_pwd;
#endif /* CONFIG_APPELFLOADER_VFSEXEC_ENVPWD */

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
	/* Cut off kernel name (argv[0]) and program name (argv[1])
	 * from argument vector
	 */
	argv = &argv[2];
	argc -= 2;

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

	/*
	 * Create thread container
	 * It will have a new stack and an ukarch_ctx
	 */
	app_thread = uk_thread_create_container(uk_alloc_get_default(),
						uk_alloc_get_default(),
				 PAGES2BYTES(CONFIG_APPELFLOADER_STACK_NBPAGES),
						uk_alloc_get_default(),
						0,
						uk_alloc_get_default(),
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
#if CONFIG_APPELFLOADER_VFSEXEC_ENVPATH
	env_path = getenv("PATH");
	if (env_path) {
		realpath = locate_exec(path, env_path);
		if (PTR2ERR(realpath) == -EINVAL) {
			realpath = NULL;
		} else if (PTRISERR(realpath) && PTR2ERR(realpath) != -EINVAL) {
			uk_pr_err("%s: Failed to find executable in envirenment ($PATH): %s (%d)",
				  progname, strerror(-PTR2ERR(realpath)),
				  PTR2ERR(realpath));
			goto out_free_thread;
		}
	}
#endif /* CONFIG_APPELFLOADER_VFSEXEC_ENVPATH */
	uk_pr_debug("%s: Load executable (%s)...\n",
		    progname, realpath ? realpath : path);
	prog = elf_load_vfs(uk_alloc_get_default(),
			    realpath ? realpath : path, progname);
#if CONFIG_APPELFLOADER_VFSEXEC_ENVPATH
	if (realpath)
		free(realpath);
#endif /* CONFIG_APPELFLOADER_VFSEXEC_ENVPATH */
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
#if CONFIG_LIBUKSWRAND
	uk_swrand_fill_buffer(rand, sizeof(rand));
#else /* !CONFIG_LIBUKSWRAND */
	/* Without random numbers, use a hardcoded seed */
	uk_pr_warn("%s: Using hard-coded random seed\n", progname);
	rand[0] = 0xB0B0;
	rand[1] = 0xF00D;
#endif /* !CONFIG_LIBUKSWRAND */

	uk_pr_debug("%s: Prepare application thread...\n", progname);
	elf_ctx_init(&app_thread->ctx, prog, progname,
		     argc, argv, environ, rand);
	app_thread->flags |= UK_THREADF_RUNNABLE;
#if CONFIG_LIBPOSIX_PROCESS
	ret = uk_posix_process_create(uk_alloc_get_default(),
				      app_thread,
				      uk_thread_current());
	if (unlikely(ret)) {
		uk_pr_err("Could not create application process: %d\n", ret);
		return ret;
	}
#endif
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
	uk_sched_thread_add(uk_sched_current(), app_thread);

#if CONFIG_LIBPOSIX_PROCESS
	/*
	 * Wait for application to exit
	 */
	ret = uk_posix_process_wait();
	goto out;
#else /* !CONFIG_LIBPOSIX_PROCESS */
	for (;;)
		sleep(10);
#endif /* !CONFIG_LIBPOSIX_PROCESS */

out_free_thread:
	uk_thread_release(app_thread);
out:
#if CONFIG_APPELFLOADER_VFSEXEC
	if (progname_conv)
		free(progname_conv);
#endif /* CONFIG_APPELFLOADER_VFSEXEC */
	return ret;
}
