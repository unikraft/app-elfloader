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
#include <fcntl.h>
#include <sys/stat.h>
#include <uk/errptr.h>
#include <uk/essentials.h>
#include <uk/plat/memory.h>
#if CONFIG_PLAT_HYPERLIGHT
#include <uk/pm.h>
#endif
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

#if CONFIG_PLAT_HYPERLIGHT
/* Forward declarations — defined in plat/hyperlight/dispatch.c */
extern void hyperlight_dispatch_register(void (*fn)(void));
extern void hyperlight_dispatch_set_elf_entry(__u64 entry);
extern __u64 hyperlight_dispatch_get_elf_entry(void);


static struct uk_thread *hyperlight_deferred_thread;
static struct uk_sched *hyperlight_deferred_sched;
extern __uptr hyperlight_kernel_fsbase; /* defined in arch/x86/sysctx.c */
static __uptr hyperlight_elf_main_addr; /* main() in loaded ELF (0 = use _start) */
static __uptr hyperlight_elf_libc_addr; /* musl __libc struct in loaded ELF */

/**
 * Find "main" and "__libc" symbols in an ELF binary's .symtab section.
 * Populates main_out/libc_out with st_value (virtual address), 0 if not found.
 */
static void find_elf_symbols(const char *path,
			     __u64 *main_out, __u64 *libc_out)
{
	int fd;
	__u8 ehdr[64];
	ssize_t n;

	*main_out = 0;
	*libc_out = 0;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return;

	/* Read ELF header (64 bytes for Elf64) */
	n = read(fd, ehdr, 64);
	if (n < 64)
		goto out;

	__u64 e_shoff = *(__u64 *)(ehdr + 40);
	__u16 e_shentsize = *(__u16 *)(ehdr + 58);
	__u16 e_shnum = *(__u16 *)(ehdr + 60);

	if (!e_shoff || !e_shnum || e_shentsize < 64)
		goto out;

	/* Iterate section headers to find SHT_SYMTAB */
	__u64 symtab_off = 0, symtab_sz = 0, symtab_entsz = 0;
	__u64 strtab_off = 0, strtab_sz = 0;
	__u32 symtab_link = 0;

	for (__u16 i = 0; i < e_shnum; i++) {
		__u8 shdr[64];

		if (lseek(fd, e_shoff + (__u64)i * e_shentsize, SEEK_SET) < 0)
			goto out;
		n = read(fd, shdr, 64);
		if (n < 64)
			goto out;

		__u32 sh_type = *(__u32 *)(shdr + 4);
		if (sh_type == 2) { /* SHT_SYMTAB */
			symtab_off = *(__u64 *)(shdr + 24);
			symtab_sz = *(__u64 *)(shdr + 32);
			symtab_link = *(__u32 *)(shdr + 40);
			symtab_entsz = *(__u64 *)(shdr + 56);
			break;
		}
	}

	if (!symtab_off || !symtab_entsz)
		goto out;

	/* Read the linked .strtab section header */
	{
		__u8 shdr[64];

		if (lseek(fd, e_shoff + (__u64)symtab_link * e_shentsize, SEEK_SET) < 0)
			goto out;
		n = read(fd, shdr, 64);
		if (n < 64)
			goto out;
		strtab_off = *(__u64 *)(shdr + 24);
		strtab_sz = *(__u64 *)(shdr + 32);
	}

	if (!strtab_off || !strtab_sz)
		goto out;

	/* Read .strtab into a buffer */
	char *strtab = malloc(strtab_sz);
	if (!strtab)
		goto out;
	if (lseek(fd, strtab_off, SEEK_SET) < 0) {
		free(strtab);
		goto out;
	}
	n = read(fd, strtab, strtab_sz);
	if (n < (ssize_t)strtab_sz) {
		free(strtab);
		goto out;
	}

	/* Iterate .symtab entries to find "main" and "__libc" */
	__u64 num_syms = symtab_sz / symtab_entsz;
	for (__u64 i = 0; i < num_syms; i++) {
		__u8 sym[24]; /* Elf64_Sym is 24 bytes */

		if (lseek(fd, symtab_off + i * symtab_entsz, SEEK_SET) < 0)
			break;
		n = read(fd, sym, 24);
		if (n < 24)
			break;

		__u32 st_name = *(__u32 *)(sym + 0);
		__u8 st_info = sym[4];
		__u64 st_value = *(__u64 *)(sym + 8);
		__u8 st_type = st_info & 0xf;
		__u8 st_bind = st_info >> 4;

		if (!st_value || st_name >= strtab_sz)
			continue;
		if (st_bind != 1) /* STB_GLOBAL */
			continue;

		const char *name = strtab + st_name;

		/* main: GLOBAL FUNC */
		if (st_type == 2 && !*main_out &&
		    name[0] == 'm' && name[1] == 'a' &&
		    name[2] == 'i' && name[3] == 'n' && name[4] == '\0') {
			*main_out = st_value;
		}
		/* __libc: GLOBAL OBJECT */
		if (st_type == 1 && !*libc_out &&
		    name[0] == '_' && name[1] == '_' &&
		    name[2] == 'l' && name[3] == 'i' &&
		    name[4] == 'b' && name[5] == 'c' && name[6] == '\0') {
			*libc_out = st_value;
		}
		if (*main_out && *libc_out)
			break;
	}

	free(strtab);
out:
	close(fd);
}

static void hyperlight_deferred_run(void)
{
	/* Step 1: IDT/IST pre-fault + MSR restore is now handled by
	 * hyperlight_dispatch_prepare() in dispatch.c, called from
	 * hyperlight_dispatch_inner() before this callback.
	 */

	/* Step 2: Restore FS base (TLS) for kernel services */
	if (hyperlight_kernel_fsbase) {
		__u32 lo = (__u32)hyperlight_kernel_fsbase;
		__u32 hi = (__u32)(hyperlight_kernel_fsbase >> 32);
		__asm__ volatile("wrmsr" : : "c"(0xC0000100), "a"(lo), "d"(hi));
	}

	/* Step 3: Restore the fd table for this TLS context */
	{
		extern void hyperlight_fdtab_restore_active(void);
		hyperlight_fdtab_restore_active();
	}

	/* Step 4: Call main() directly or jump to _start as fallback */
	if (hyperlight_elf_main_addr) {
		/* Parse argc/argv/envp from the prepared stack.
		 * ukarch_ctx_init() pushes 2 context frames (entry + call0)
		 * below the application stack, so ctx.sp is 16 bytes below argc.
		 */
		__uptr sp = hyperlight_deferred_thread->ctx.sp + 16;
		int argc = (int)(*(long *)sp);
		char **argv = (char **)(sp + sizeof(long));
		char **envp = argv + argc + 1;

		/* Set musl libc.auxv so malloc/stdio work without __init_libc */
		if (hyperlight_elf_libc_addr) {
			char **p = envp;
			while (*p) p++;
			p++; /* skip NULL terminator */
			*((__u64 *)(hyperlight_elf_libc_addr + 8)) = (__u64)p;
		}

		typedef int (*main_fn_t)(int, char **, char **);
		main_fn_t fn = (main_fn_t)hyperlight_elf_main_addr;
		fn(argc, argv, envp);
		/* main() returned — dispatch completes normally */
	} else {
		/* No main() found — fall back to _start dispatch.
		 * ukarch_ctx_init() pushes 2 context frames (entry + call0)
		 * below the application stack, so ctx.sp is 16 bytes below argc.
		 */
		__uptr entry = (__uptr)hyperlight_dispatch_get_elf_entry();
		__uptr orig_sp = hyperlight_deferred_thread->ctx.sp + 16;
		__asm__ volatile(
			"movq %0, %%rsp\n\t"
			"xorq %%rbp, %%rbp\n\t"
			"jmpq *%1\n\t"
			: : "r"(orig_sp), "r"(entry)
			: "memory"
		);
		__builtin_unreachable();
	}
}
#endif /* CONFIG_PLAT_HYPERLIGHT */

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

#if CONFIG_LIBPOSIX_PROCESS_EXECVE && CONFIG_APPELFLOADER_VFSEXEC
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

	/* NOTE: elf_ctx_init() prepends progname as argv[0], so we
	 * do NOT need to manually prepend path here.  argv currently
	 * contains just the app arguments (e.g., ["/demo.sh"]).
	 */

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

#if CONFIG_LIBPOSIX_PROCESS_EXECVE && CONFIG_APPELFLOADER_VFSEXEC \
    && !CONFIG_PLAT_HYPERLIGHT
	/* On Hyperlight we skip execve() and use the deferred-dispatch path
	 * (load ELF, create thread, register dispatch callback, return).
	 * execve() would run the binary immediately during evolve, bypassing
	 * the snapshot/restore/call lifecycle.
	 */
	ret = execve(realpath ? realpath : path,
		     (char * const *)argv, (char * const *)environ);
	if (unlikely(ret)) {
		uk_pr_err("Could not execve (%d)\n", ret);
		goto out;
	}
#endif /* CONFIG_LIBPOSIX_PROCESS_EXECVE && !CONFIG_PLAT_HYPERLIGHT */

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

	const char **eff_environ = environ;
#if CONFIG_PLAT_HYPERLIGHT
	/* Stuff two pointer-slot addresses into the loaded ELF's env so a
	 * dynamically-linked driver that doesn't link against kernel
	 * symbols can still read the current in-flight FunctionCall bytes:
	 *   HL_FC_BYTES_PTR=0x<addr of slot holding (const u8 *)>
	 *   HL_FC_LEN_PTR=0x<addr of slot holding (size_t)>
	 * Both addresses are kernel .data globals — stable across snapshot
	 * and restore, so the driver reads them once at init and then just
	 * dereferences on every dispatch.
	 */
	extern const __u8 **hyperlight_dispatch_fc_bytes_slot(void);
	extern __sz *hyperlight_dispatch_fc_len_slot(void);
	typedef void (*hl_dispatch_fn)(const __u8 *, __sz);
	extern hl_dispatch_fn *hyperlight_dispatch_v2_slot(void);
	static char hl_fc_bytes_env[48];
	static char hl_fc_len_env[48];
	static char hl_v2_cb_env[52];

	snprintf(hl_fc_bytes_env, sizeof(hl_fc_bytes_env),
		 "HL_FC_BYTES_PTR=0x%lx",
		 (unsigned long)hyperlight_dispatch_fc_bytes_slot());
	snprintf(hl_fc_len_env, sizeof(hl_fc_len_env),
		 "HL_FC_LEN_PTR=0x%lx",
		 (unsigned long)hyperlight_dispatch_fc_len_slot());
	snprintf(hl_v2_cb_env, sizeof(hl_v2_cb_env),
		 "HL_V2_CALLBACK_PTR=0x%lx",
		 (unsigned long)hyperlight_dispatch_v2_slot());

	/* Count existing environ entries and build a merged array. The
	 * buffer is static to avoid heap churn — elf_ctx_init copies the
	 * strings into its infoblk so the pointers only need to outlive
	 * that call.
	 */
	int base_envc = 0;
	if (environ) {
		while (environ[base_envc])
			base_envc++;
	}

	static const char *hl_merged_env[64];
	const int hl_extra = 3;
	if (base_envc + hl_extra + 1 <= (int)ARRAY_SIZE(hl_merged_env)) {
		for (int i = 0; i < base_envc; i++)
			hl_merged_env[i] = environ[i];
		hl_merged_env[base_envc]     = hl_fc_bytes_env;
		hl_merged_env[base_envc + 1] = hl_fc_len_env;
		hl_merged_env[base_envc + 2] = hl_v2_cb_env;
		hl_merged_env[base_envc + 3] = NULL;
		eff_environ = hl_merged_env;
	} else {
		uk_pr_warn("%s: skipping HL_FC_*_PTR env injection; "
			   "too many existing env vars\n", progname);
	}
#endif /* CONFIG_PLAT_HYPERLIGHT */

	ret = elf_arg_env_count(&argc, argv, &envc, eff_environ,
				PAGES2BYTES(CONFIG_APPELFLOADER_STACK_NBPAGES));
	if (unlikely(ret < 0)) {
		uk_pr_err("Args + env size exceeds limit, increase stack size\n");
		goto out_free_thread;
	}

	elf_ctx_init(&app_thread->ctx, prog, progname,
		     argc, argv, envc, eff_environ, rand);
#if !CONFIG_PLAT_HYPERLIGHT
	/* Mark the thread as runnable for the scheduler.
	 * On Hyperlight, the dispatch callback runs the thread directly
	 * (bypassing the scheduler), so we must NOT set RUNNABLE here —
	 * otherwise create_pthread would add it to the scheduler and it
	 * would execute during evolve before dispatch is registered.
	 */
	app_thread->flags |= UK_THREADF_RUNNABLE;
#endif

#if CONFIG_LIBPOSIX_PROCESS_MULTITHREADING
	/* Add application thread to process */
	ret = uk_posix_process_create_pthread(app_thread);
	if (unlikely(ret)) {
		uk_pr_err("Could not create pthread\n");
		goto out_free_thread;
	}
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
#if CONFIG_PLAT_HYPERLIGHT
	/*
	 * On Hyperlight, defer execution: store the app thread and
	 * scheduler so the dispatch function can run them on call().
	 * Return from main() — boot.c shutdown signals readiness to
	 * the host with the dispatch function address in RAX.
	 */
	hyperlight_deferred_thread = app_thread;
	hyperlight_deferred_sched = s;
	/* Store the ELF entry address for use after restore.
	 * For dynamically-linked binaries, use the interpreter's entry —
	 * the dynamic linker must run before the main binary's _start.
	 */
	if (prog->interp.required)
		hyperlight_dispatch_set_elf_entry(
			(__u64)prog->interp.prog->entry);
	else
		hyperlight_dispatch_set_elf_entry((__u64)prog->entry);
	/* Find main() and __libc in the ELF symbol table for direct dispatch.
	 * Calling main() directly avoids _start which triggers musl's TLS init
	 * (overriding FS_BASE) and PIE re-relocation.
	 * __libc is needed to set libc.auxv so malloc/stdio work.
	 *
	 * Skip for dynamically-linked binaries (interp.required): the dynamic
	 * linker must run first to resolve GOT/PLT entries before main() can
	 * be called.  Fall through to the _start path instead.
	 */
	if (!prog->interp.required) {
		__u64 main_vaddr, libc_vaddr;
		find_elf_symbols(prog->path, &main_vaddr, &libc_vaddr);
		if (main_vaddr)
			hyperlight_elf_main_addr = (__uptr)prog->vabase + main_vaddr;
		if (libc_vaddr)
			hyperlight_elf_libc_addr = (__uptr)prog->vabase + libc_vaddr;
	}
	/* Leave .dynamic intact: the elfloader already applied RELATIVE
	 * and IRELATIVE relocations, and glibc/musl re-applying them
	 * during _start is idempotent.  Keeping .dynamic allows libc
	 * to find DT_INIT_ARRAY and complete its own initialization
	 * (TLS, locale, etc.) when entering via _start.
	 */
	/* Re-save LSTAR here (after _init_syscall has set it during boot) */
	{
		extern void hyperlight_dispatch_save_msrs(void);
		hyperlight_dispatch_save_msrs();
	}
	/* Save FS base (TLS) via rdmsr — needed after restore for kernel services */
	{
		__u32 lo, hi;
		__asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000100));
		hyperlight_kernel_fsbase = ((__uptr)hi << 32) | lo;
	}
	hyperlight_dispatch_register(hyperlight_deferred_run);
	/* Halt directly — do not return to boot.c's shutdown machinery.
	 * The cooperative scheduler's idle thread would do a bare HLT
	 * (without outl 108), which the host misinterprets.
	 * ukplat_terminate goes through the proper Hyperlight halt path:
	 * push void result → load dispatch_function into RAX → outl 108.
	 */
	uk_pm_shutdown(UK_PM_SHUTDOWN_OP_SYSHALT);
#else
	uk_sched_thread_add(s, app_thread);

	/*
	 * FIXME: Instead of an infinite wait, wait for application
	 *        to exit (this needs thread_wait support with
	 *        uksched and/or posix-process)
	 */
	for (;;)
		sleep(10);
#endif

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
	return ret;
}
