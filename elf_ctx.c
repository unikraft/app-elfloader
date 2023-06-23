/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Authors: Pierre Oliver <pierre.olivier@manchester.ac.uk>
 *          Stefan Lankes <slankes@eonerc.rwth-aachen.de>
 *          Simon Kuenzer <simon.kuenzer@neclab.eu>
 *
 * Copyright (c) 2019, Virginia Polytechnic Institute and State University.
 *                     All rights reserved.
 * Copyright (c) 2019, RWTH Aachen University, Germany.
 *.                    All rights reserved.
 * Copyright (c) 2019, NEC Laboratories Europe GmbH,
 *                     NEC Corporation. All rights reserved.
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
/*
 * With the approval of the original authors, some code in this file was
 * derived and adopted from:
 * HermiTux
 * https://github.com/ssrg-vt/hermitux, commit 8780d335
 * File: apps/hermitux-light/hermitux-light.c
 * The authors of the original implementation are attributed in the license
 * header of this file.
 */

#include <uk/plat/bootstrap.h>
#include <uk/assert.h>
#include <uk/print.h>
#include <uk/essentials.h>

#include "elf_prog.h"

/* Fields for auxiliary vector
 * (https://lwn.net/Articles/519085/)
 */
#define AT_NULL			0
#define AT_IGNORE		1
#define AT_EXECFD		2
#define AT_PHDR			3
#define AT_PHENT		4
#define AT_PHNUM		5
#define AT_PAGESZ		6
#define AT_BASE			7
#define AT_FLAGS		8
#define AT_ENTRY		9
#define AT_NOTELF		10
#define AT_UID			11
#define AT_EUID			12
#define AT_GID			13
#define AT_EGID			14
#define AT_PLATFORM		15
#define AT_HWCAP		16
#define AT_CLKTCK		17
#define AT_DCACHEBSIZE		19
#define AT_ICACHEBSIZE		20
#define AT_UCACHEBSIZE		21
#define AT_SECURE		23
#define AT_RANDOM		25
#define AT_EXECFN		31
#define AT_SYSINFO_EHDR		33
#define AT_SYSINFO		32

struct auxv_entry {
	long key;
	long val;
};

#if CONFIG_VDSO
extern char *vdso_image_addr;
#endif

#if CONFIG_ARCH_X86_64
	static const char *auxv_platform = "x86_64";
#else
#error "Unsupported architecture"
#endif /* CONFIG_ARCH_X86_64 */

void elf_ctx_init(struct ukarch_ctx *ctx, struct elf_prog *prog,
		  const char *argv0, int argc, char *argv[], char *environ[],
		  uint64_t rand[2])
{
	int i, envc, elfvec_len;

	UK_ASSERT(prog);
	UK_ASSERT(argv0 || ((argc >= 1) && argv));

	uk_pr_debug("%s: image:          0x%"PRIx64" - 0x%"PRIx64"\n",
		    prog->name, (uint64_t) prog->vabase,
		    (uint64_t) prog->vabase + prog->valen);
	uk_pr_debug("%s: start:          0x%"PRIx64"\n", prog->name,
		    (uint64_t) prog->start);
	uk_pr_debug("%s: entry:          0x%"PRIx64"\n", prog->name,
		    (uint64_t) prog->entry);
	uk_pr_debug("%s: phdr.off:       0x%"PRIx64"\n", prog->name,
		    (uint64_t) prog->phdr.off);
	uk_pr_debug("%s: phdr.num:       %"PRIu64"\n",   prog->name,
		    (uint64_t) prog->phdr.num);
	uk_pr_debug("%s: phdr.entsize:   0x%"PRIx64"\n", prog->name,
		    (uint64_t) prog->phdr.entsize);
	if (prog->interp.prog) {
		uk_pr_debug("%s: interp:         0x%"PRIx64" - 0x%"PRIx64"\n",
			    prog->name, (uint64_t) prog->interp.prog->vabase,
			    (uint64_t) prog->interp.prog->vabase +
			    prog->interp.prog->valen);
		uk_pr_debug("%s: interp.start:   0x%"PRIx64"\n", prog->name,
			    (uint64_t) prog->interp.prog->start);
		uk_pr_debug("%s: interp.entry:   0x%"PRIx64"\n", prog->name,
			    (uint64_t) prog->interp.prog->entry);
	}

	/* count the number of environment variables */
	envc = 0;
	if (environ)
		for (char **env = environ; *env; ++env)
			++envc;

	/* list of all auxiliary vector entries */
	struct auxv_entry auxv[] = {
		{ AT_PLATFORM, (uintptr_t) auxv_platform },
		{ AT_NOTELF, 0x0 },
		{ AT_UCACHEBSIZE, 0x0 },
		{ AT_ICACHEBSIZE, 0x0 },
		{ AT_DCACHEBSIZE, 0x0 },
		/* path to executable */
		{ AT_EXECFN, (long) (prog->path ? prog->path : prog->name) },
		{ AT_SECURE, 0x0 },
		{ AT_EGID, 0x0 },
		{ AT_GID, 0x0 },
		{ AT_EUID, 0x0 },
		{ AT_UID, 0x0 },
		{ AT_ENTRY, prog->entry },
		{ AT_FLAGS, 0x0 },
		{ AT_CLKTCK, 0x64 }, /* Mimic Linux */
		{ AT_HWCAP, 0x0 },
		{ AT_PAGESZ, 4096 },
		/* base addr of interpreter */
		{ AT_BASE, prog->interp.prog ?
			   prog->interp.prog->start : 0x0 },
		{ AT_RANDOM, (uintptr_t) rand },
		{ AT_PHENT, prog->phdr.entsize },
		{ AT_PHNUM, prog->phdr.num },
		{ AT_PHDR, prog->start + prog->phdr.off },
#if CONFIG_VDSO
		{ AT_SYSINFO_EHDR, (long)vdso_image_addr},
#endif
		{ AT_IGNORE, 0x0 }
	};
	struct auxv_entry auxv_null = { AT_NULL, 0x0 };

	/*
	 * We need to respect the stack alignment ABI requirements at function
	 * calls. (eg: x86_64 requires that the stack is 16-byte aligned)
	 *
	 * Here we count how many bytes are pushed to the stack:
	 * - auxiliary vector;
	 * - environment variables (plus extra NULL pointer);
	 * - arguments (plus extra NULL pointer and argument count (long))
	 */
	elfvec_len = ((ARRAY_SIZE(auxv) + 1) * sizeof(struct auxv_entry))
		+ (envc + 1) * sizeof(uintptr_t)
		+ (argc + (argv0 ? 1 : 0) + 1) * sizeof(uintptr_t)
		+ sizeof(long);

	ctx->sp = ALIGN_DOWN(ctx->sp - elfvec_len, UKARCH_SP_ALIGN)
		+ elfvec_len;

	/*
	 * We need to push the element on the stack in the inverse order they
	 * will be read by the application's C library (i.e. argc in the end)
	 *
	 * Auxiliary vector (NOTE: we push the terminating NULL first)
	 */
	ukarch_rctx_stackpush(ctx, auxv_null);
	for (i = (int) ARRAY_SIZE(auxv) - 1; i >= 0; --i)
		ukarch_rctx_stackpush(ctx, auxv[i]);

	/*
	 * envp
	 */
	/* NOTE: As expected, this will push NULL to the stack first */
	ukarch_rctx_stackpush(ctx, (long) NULL);
	if (environ) {
		for (i = envc-1; i >= 0; --i) {
			uk_pr_debug("env[%d]=\"%s\"\n", i, environ[i]);
			ukarch_rctx_stackpush(ctx, (uintptr_t) environ[i]);
		}
	}

	/*
	 * argv + argc
	 */
	/* Same as envp, pushing NULL first */
	ukarch_rctx_stackpush(ctx, (long) NULL);
	if (argc)
		for (i = argc - 1; i >= 0; --i)
			ukarch_rctx_stackpush(ctx, (uintptr_t) argv[i]);
	if (argv0)
		ukarch_rctx_stackpush(ctx, (uintptr_t) argv0);
	ukarch_rctx_stackpush(ctx, (long) argc + (argv0 ? 1 : 0));

	UK_ASSERT(IS_ALIGNED(ctx->sp, UKARCH_SP_ALIGN));

	/* ctx will enter the entry point with cleared registers. */
	if (prog->interp.required) {
		struct elf_prog *interp = prog->interp.prog;

		UK_ASSERT(prog->interp.prog);

		/* dynamically linked executable, jump into loader instead */
		ukarch_ctx_init(ctx, ctx->sp, 0x0, interp->entry);
	} else {
		/* statically linked executable */
		ukarch_ctx_init(ctx, ctx->sp, 0x0, prog->entry);
	}
}
