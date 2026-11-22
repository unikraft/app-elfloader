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

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <uk/assert.h>
#include <uk/print.h>
#include <uk/essentials.h>
#if CONFIG_ARCH_ARM_64
#include <bits/hwcap.h>
#include <uk/arch/arm64.h>
#include <uk/arch/util.h>
#endif /* CONFIG_ARCH_ARM_64 */

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

#if CONFIG_APPELFLOADER_VDSO
extern char *vdso_image_addr;
#endif /* CONFIG_APPELFLOADER_VDSO */

#if CONFIG_ARCH_X86_64
#define UK_AUXV_PLATFORM	"x86_64"
#elif CONFIG_ARCH_ARM_64
#define UK_AUXV_PLATFORM	"aarch64"
#else
#error "Unsupported architecture"
#endif

#if CONFIG_ARCH_ARM_64
#define ID_FIELD(val, reg, field)					\
	(((val) >> UK_ARCH_ARM64_##reg##_##field##_SHIFT) &		\
	 UK_ARCH_ARM64_##reg##_##field##_MASK)

/* Signed 4-bit field: 0xf reads as -1 ("not implemented") */
#define ID_SFIELD(val, reg, field)					\
	((int)((__s64)((val) <<						\
	 (60 - UK_ARCH_ARM64_##reg##_##field##_SHIFT)) >> 60))

/*
 * AT_HWCAP as Linux reports it on arm64 (uapi/asm/hwcap.h), from the ID
 * registers: userspace learns the CPU's features from it, and some
 * programs refuse to run without it (.NET needs ASIMD).  Left out are
 * features whose state the kernel would have to manage for the program
 * (SVE, pointer authentication keys, the timer event stream) and CPUID:
 * at EL1 the ID registers read unfiltered, so they would advertise
 * features Unikraft leaves disabled (SME, say).  FP and Advanced SIMD
 * instructions, the crypto ones included, need CONFIG_FPSIMD.
 */
static __u64 elf_arm64_hwcap(void)
{
	__u64 pfr0 = UK_ARCH_ARM64_SYSREG_READ64(id_aa64pfr0_el1);
	__u64 pfr1 = UK_ARCH_ARM64_SYSREG_READ64(id_aa64pfr1_el1);
	__u64 isar0 = UK_ARCH_ARM64_SYSREG_READ64(id_aa64isar0_el1);
	__u64 isar1 = UK_ARCH_ARM64_SYSREG_READ64(id_aa64isar1_el1);
	__u64 mmfr2 =
		UK_ARCH_ARM64_SYSREG_READ64(UK_ARCH_ARM64_ID_AA64MMFR2_EL1);
	__u64 hwcap = 0;

#if CONFIG_FPSIMD
	if (ID_SFIELD(pfr0, ID_AA64PFR0_EL1, FP) >= 0)
		hwcap |= HWCAP_FP;
	if (ID_SFIELD(pfr0, ID_AA64PFR0_EL1, FP) >= 1)
		hwcap |= HWCAP_FPHP;
	if (ID_SFIELD(pfr0, ID_AA64PFR0_EL1, ADVSIMD) >= 0)
		hwcap |= HWCAP_ASIMD;
	if (ID_SFIELD(pfr0, ID_AA64PFR0_EL1, ADVSIMD) >= 1)
		hwcap |= HWCAP_ASIMDHP;
	if (ID_FIELD(isar0, ID_AA64ISAR0_EL1, AES) >= 1)
		hwcap |= HWCAP_AES;
	if (ID_FIELD(isar0, ID_AA64ISAR0_EL1, AES) >= 2)
		hwcap |= HWCAP_PMULL;
	if (ID_FIELD(isar0, ID_AA64ISAR0_EL1, SHA1) >= 1)
		hwcap |= HWCAP_SHA1;
	if (ID_FIELD(isar0, ID_AA64ISAR0_EL1, SHA2) >= 1)
		hwcap |= HWCAP_SHA2;
	if (ID_FIELD(isar0, ID_AA64ISAR0_EL1, SHA2) >= 2)
		hwcap |= HWCAP_SHA512;
	if (ID_FIELD(isar0, ID_AA64ISAR0_EL1, RDM) >= 1)
		hwcap |= HWCAP_ASIMDRDM;
	if (ID_FIELD(isar0, ID_AA64ISAR0_EL1, SHA3) >= 1)
		hwcap |= HWCAP_SHA3;
	if (ID_FIELD(isar0, ID_AA64ISAR0_EL1, SM3) >= 1)
		hwcap |= HWCAP_SM3;
	if (ID_FIELD(isar0, ID_AA64ISAR0_EL1, SM4) >= 1)
		hwcap |= HWCAP_SM4;
	if (ID_FIELD(isar0, ID_AA64ISAR0_EL1, DP) >= 1)
		hwcap |= HWCAP_ASIMDDP;
	if (ID_FIELD(isar0, ID_AA64ISAR0_EL1, FHM) >= 1)
		hwcap |= HWCAP_ASIMDFHM;
	if (ID_FIELD(isar1, ID_AA64ISAR1_EL1, JSCVT) >= 1)
		hwcap |= HWCAP_JSCVT;
	if (ID_FIELD(isar1, ID_AA64ISAR1_EL1, FCMA) >= 1)
		hwcap |= HWCAP_FCMA;
#endif /* CONFIG_FPSIMD */
	if (ID_FIELD(pfr0, ID_AA64PFR0_EL1, DIT) >= 1)
		hwcap |= HWCAP_DIT;
	if (ID_FIELD(isar0, ID_AA64ISAR0_EL1, CRC32) >= 1)
		hwcap |= HWCAP_CRC32;
	if (ID_FIELD(isar0, ID_AA64ISAR0_EL1, ATOMIC) >= 2)
		hwcap |= HWCAP_ATOMICS;
	if (ID_FIELD(isar0, ID_AA64ISAR0_EL1, TS) >= 1)
		hwcap |= HWCAP_FLAGM;
	if (ID_FIELD(isar1, ID_AA64ISAR1_EL1, DPB) >= 1)
		hwcap |= HWCAP_DCPOP;
	if (ID_FIELD(isar1, ID_AA64ISAR1_EL1, LRCPC) >= 1)
		hwcap |= HWCAP_LRCPC;
	if (ID_FIELD(isar1, ID_AA64ISAR1_EL1, LRCPC) >= 2)
		hwcap |= HWCAP_ILRCPC;
	if (ID_FIELD(isar1, ID_AA64ISAR1_EL1, SB) >= 1)
		hwcap |= HWCAP_SB;
	if (ID_FIELD(mmfr2, ID_AA64MMFR2_EL1, AT) >= 1)
		hwcap |= HWCAP_USCAT;
	if (ID_FIELD(pfr1, ID_AA64PFR1_EL1, SSBS) >= 2)
		hwcap |= HWCAP_SSBS;
	return hwcap;
}
#define UK_AUXV_HWCAP		elf_arm64_hwcap()
#else /* !CONFIG_ARCH_ARM_64 */
#define UK_AUXV_HWCAP		0x0
#endif /* !CONFIG_ARCH_ARM_64 */

static void infoblk_push(struct ukarch_ctx *ctx, void *buf, __sz len)
{
	UK_ASSERT(ctx);
	UK_ASSERT(ctx->sp);
	UK_ASSERT(buf);

	ctx->sp -= len + 1;
	memcpy((void *)ctx->sp, buf, len);
	((char *)ctx->sp)[len] = '\0';
}

int elf_arg_env_count(int *argc, const char **argv,
		      int *envc, const char **envp,
		      __sz stack_sz)
{
	const char **a;
	__sz len, size, limit = MAX((__sz)ARG_MAX, stack_sz / 4);

	limit = MIN(limit, (__sz)INT_MAX);
	*argc = *envc = len = 0;
	if (!argv && !envp)
		return 0;

	/* count argv */
	if (argv)
		for (a = argv; *a; ++a) {
			if (*argc == INT_MAX)
				return -E2BIG;
			size = strnlen(*a, limit - len + 1);
			if (size > limit - len)
				return -E2BIG;
			++*argc;
			len += size;
		}

	/* count envp */
	if (envp)
		for (a = envp; *a; ++a) {
			if (*envc == INT_MAX)
				return -E2BIG;
			size = strnlen(*a, limit - len + 1);
			if (size > limit - len)
				return -E2BIG;
			++*envc;
			len += size;
		}

	/* POSIX defines the minimum total size of argv + envp to ARG_MAX,
	 * and leaves the inclusion of NULL terminators, pointers and / or
	 * alignment bytes as implementation defined. The policy we implement
	 * is:
	 *
	 * - Permit the total length to exceed ARG_MAX as long as it's not
	 * greater than 1/4 of the stack size. This is essentially the same
	 * policy as linux (see execve(2)), with the difference that we
	 * don't apply the MAX_ARG_STRLEN floor of 32 pages.
	 *
	 * - Exclude auxv, NULL, alignment bytes from the checked size, as
	 * these are of fixed size and occupy an insignificant portion of
	 * the stack.
	 */
	UK_ASSERT(IS_ALIGNED(stack_sz, 4));
	if (unlikely(len > ARG_MAX && len > stack_sz / 4))
		return -E2BIG;

	return len;
}

/* Leave room for all auxiliary entries, random seed and stack alignment. */
#define ELF_CTX_AUXV_MAX 32
#define ELF_CTX_FIXED_BYTES \
	(ELF_CTX_AUXV_MAX * sizeof(struct auxv_entry) + \
	 sizeof(UK_AUXV_PLATFORM) + 2 * sizeof(uint64_t) + 3 + \
	 2 * UKARCH_SP_ALIGN + 2 * sizeof(uintptr_t))

static int elf_stack_string(__sz *left, const char *string)
{
	__sz len;

	if (!*left)
		return -E2BIG;
	len = strnlen(string, *left);
	if (len == *left)
		return -E2BIG;
	*left -= len + 1;
	return 0;
}

int elf_ctx_check_args(struct elf_prog *prog, const char *argv0,
		       int *argc, const char *argv[], int *envc,
		       const char *envp[], __sz stack_sz)
{
	__sz left, slots;
	int bytes, rc;

	bytes = elf_arg_env_count(argc, argv, envc, envp, stack_sz);
	if (bytes < 0)
		return bytes;
	if (!argv0 && !*argc)
		return -EINVAL;
	if (*argc == INT_MAX)
		return -E2BIG;
	if (stack_sz < ELF_CTX_FIXED_BYTES)
		return -E2BIG;
	left = stack_sz - ELF_CTX_FIXED_BYTES;
	/* argc, both vector terminators, and an optional prepended argument. */
	slots = (__sz)*argc + (__sz)*envc + 3 + !!argv0;
	if (slots > left / sizeof(uintptr_t))
		return -E2BIG;
	left -= slots * sizeof(uintptr_t);
	if ((__sz)bytes > left)
		return -E2BIG;
	left -= bytes;
	/* The count above excludes each string's terminating byte. */
	if ((__sz)*argc + (__sz)*envc > left)
		return -E2BIG;
	left -= (__sz)*argc + (__sz)*envc;
	if (argv0) {
		rc = elf_stack_string(&left, argv0);
		if (rc)
			return rc;
	}
	return elf_stack_string(&left, prog->path ? prog->path : prog->name);
}

static void elf_ctx_push_vector(struct ukarch_ctx *ctx, uintptr_t strings,
				int count)
{
	uintptr_t *vector;
	int i;

	ctx->sp -= ((__sz)count + 1) * sizeof(*vector);
	vector = (uintptr_t *)ctx->sp;
	for (i = 0; i < count; i++) {
		vector[i] = strings;
		strings += strlen((const char *)strings) + 1;
	}
	vector[count] = 0;
}

void elf_ctx_init(struct ukarch_ctx *ctx, struct elf_prog *prog,
		  const char *argv0, int argc, const char *argv[],
		  int envc, const char *environ[],
		  uint64_t rand[2])
{
	int args_count = argc + (argv0 ? 1 : 0);
	uintptr_t random_sp, argv_sp, envp_sp;
	__sz elfvec_len;
	int i;

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

	/* Keep the seed in the application's stack, independently of callers.
	 */
	infoblk_push(ctx, rand, 2 * sizeof(rand[0]));
	random_sp = ctx->sp;

	/* list of all auxiliary vector entries whose pointer values need
	 * to point to information block areas
	 */
	struct auxv_entry infoblk_auxv[] = {
		{ AT_PLATFORM, (__uptr)UK_AUXV_PLATFORM	},
		{ AT_EXECFN, (long)(prog->path ? prog->path : prog->name) },
	};

	/* list of all auxiliary vector entries whose pointer values need
	 * not be in the information block
	 */
	struct auxv_entry auxv[] = {
		{ AT_NOTELF, 0x0 },
		{ AT_UCACHEBSIZE, 0x0 },
		{ AT_ICACHEBSIZE, 0x0 },
		{ AT_DCACHEBSIZE, 0x0 },
		{ AT_SECURE, 0x0 },
		{ AT_EGID, 0x0 },
		{ AT_GID, 0x0 },
		{ AT_EUID, 0x0 },
		{ AT_UID, 0x0 },
		{ AT_ENTRY, prog->entry },
		{ AT_FLAGS, 0x0 },
		{ AT_CLKTCK, 0x64 }, /* Mimic Linux */
		{ AT_HWCAP, UK_AUXV_HWCAP },
		{ AT_PAGESZ, 4096 },
		/* base addr of interpreter */
		{ AT_BASE, prog->interp.prog ?
			   prog->interp.prog->start : 0x0 },
		{ AT_RANDOM, random_sp },
		{ AT_PHENT, prog->phdr.entsize },
		{ AT_PHNUM, prog->phdr.num },
		{ AT_PHDR, (__uptr)prog->vabase + prog->phdr.off },
#if CONFIG_APPELFLOADER_VDSO
		/* TODO: This must also be pushed and copied
		 * or mapped to information block. Move it to infoblk_auxv
		 * ASAP.
		 */
		{ AT_SYSINFO_EHDR, (uintptr_t)vdso_image_addr },
#endif /* CONFIG_APPELFLOADER_VDSO */
		{ AT_IGNORE, 0x0 }
	};
	struct auxv_entry auxv_null = { AT_NULL, 0x0 };

	UK_CTASSERT(ARRAY_SIZE(auxv) + ARRAY_SIZE(infoblk_auxv) + 1 <=
		    ELF_CTX_AUXV_MAX);

	for (i = (int)ARRAY_SIZE(infoblk_auxv) - 1; i >= 0; i--) {
		infoblk_push(ctx, (void *)infoblk_auxv[i].val,
			     strlen((const char*)infoblk_auxv[i].val));
		/* Overwrite previous pointer to our memory to a pointer to
		 * the newly copied buffer in the information block.
		 */
		infoblk_auxv[i].val = ctx->sp;
	}

	if (envc)
		for (i = envc - 1; i >= 0; i--) {
			infoblk_push(ctx, (void *)environ[i],
				     strlen(environ[i]));
		}
	envp_sp = ctx->sp;

	if (argc)
		for (i = argc - 1; i >= 0; i--)
			infoblk_push(ctx, (void *)argv[i], strlen(argv[i]));

	if (argv0)
		infoblk_push(ctx, (void *)argv0, strlen(argv0));
	argv_sp = ctx->sp;

	/* Add a NULL terminator before argv0 as the cherry on top (bottom?)
	 * and re-align stack to prepare it for what's about to be pushed
	 * next.
	 */
	ctx->sp--;
	((char *)ctx->sp)[0] = '\0';
	ctx->sp--;
	ctx->sp = ALIGN_DOWN(ctx->sp, UKARCH_SP_ALIGN);

	/*
	 * We need to respect the stack alignment ABI requirements at function
	 * calls. (eg: x86_64 requires that the stack is 16-byte aligned)
	 *
	 * Here we count how many bytes are pushed to the stack:
	 * - auxiliary vector;
	 * - environment variables (plus extra NULL pointer);
	 * - arguments (plus extra NULL pointer and argument count (long))
	 */
	elfvec_len = ((ARRAY_SIZE(auxv) + ARRAY_SIZE(infoblk_auxv) + 1) *
		      sizeof(struct auxv_entry))
		+ ((__sz)envc + 1) * sizeof(uintptr_t)
		+ ((__sz)args_count + 1) * sizeof(uintptr_t)
		+ sizeof(long);

	ctx->sp = ALIGN_DOWN(ctx->sp - elfvec_len, UKARCH_SP_ALIGN)
		+ elfvec_len;

	/*
	 * We need to push the element on the stack in the inverse order they
	 * will be read by the application's C library (i.e. argc in the end)
	 *
	 * Auxiliary vector (NOTE: we push the terminating NULL first)
	 */
	ukarch_rctx_stackpush_packed(ctx, auxv_null);
	for (i = (int)ARRAY_SIZE(infoblk_auxv) - 1; i >= 0; i--)
		ukarch_rctx_stackpush_packed(ctx, infoblk_auxv[i]);
	for (i = (int)ARRAY_SIZE(auxv) - 1; i >= 0; --i)
		ukarch_rctx_stackpush_packed(ctx, auxv[i]);

	/* Build vectors in the user stack with bounded kernel storage. */
	elf_ctx_push_vector(ctx, envp_sp, envc);
	elf_ctx_push_vector(ctx, argv_sp, args_count);
	ukarch_rctx_stackpush_packed(ctx, (long)args_count);

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
