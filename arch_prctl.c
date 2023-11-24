/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2018, Pierre Olivier, Virginia Polytechnic Institute and
 * State University. All rights reserved.
 * Copyright (c) 2019, Simon Kuenzer <simon.kuenzer@neclab.eu>,
 * NEC Laboratories Europe GmbH, NEC Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the University nor the names of its contributors
 *      may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Some code in this file was derived and adopted from:
 * HermiTux Kernel
 * https://github.com/ssrg-vt/hermitux-kernel, commit 2a3264d
 * File: kernel/syscalls/arch_prctl.c
 * The author of the original implementation is attributed in the license
 * header of this file. HermiTux kernel was released under BSD 3-clause
 * (see file /LICENSE in the HermiTux Kernel repository).
 */

#include <errno.h>
#include <stddef.h>
#include <uk/print.h>
#include <uk/syscall.h>

#define ARCH_SET_GS		0x1001
#define ARCH_SET_FS		0x1002
#define ARCH_GET_FS		0x1003
#define ARCH_GET_GS		0x1004

#define ARCH_GET_CPUID		0x1011
#define ARCH_SET_CPUID		0x1012

#define ARCH_MAP_VDSO_X32	0x2001
#define ARCH_MAP_VDSO_32	0x2002
#define ARCH_MAP_VDSO_64	0x2003

#ifdef CONFIG_ARCH_X86_64
/*
 * Move this to a ukplat_ API call
 * This works now for bare metal only
 */

#define X86_MSR_FS_BASE         0xc0000100
#define X86_MSR_GS_BASE         0xc0000101

static inline void rdmsr(unsigned int msr, __u32 *lo, __u32 *hi)
{
	asm volatile("rdmsr" : "=a"(*lo), "=d"(*hi)
			     : "c"(msr));
}

static inline __u64 rdmsrl(unsigned int msr)
{
	__u32 lo, hi;

	rdmsr(msr, &lo, &hi);
	return ((__u64) lo | (__u64) hi << 32);
}

static inline void wrmsr(unsigned int msr, __u32 lo, __u32 hi)
{
	asm volatile("wrmsr"
			     : /* no outputs */
			     : "c"(msr), "a"(lo), "d"(hi));
}

static inline void wrmsrl(unsigned int msr, __u64 val)
{
	wrmsr(msr, (__u32) (val & 0xffffffffULL), (__u32) (val >> 32));
}

static inline void writefs(__uptr fs)
{
	wrmsrl(X86_MSR_FS_BASE, fs);
}

static inline __uptr readfs(void)
{
	return rdmsrl(X86_MSR_FS_BASE);
}

static inline void writegs(__uptr gs)
{
	wrmsrl(X86_MSR_GS_BASE, gs);
}

static inline __uptr readgs(void)
{
	return rdmsrl(X86_MSR_GS_BASE);
}
#endif

UK_LLSYSCALL_R_U_DEFINE(long, arch_prctl, long, code, long, addr, long, arg2)
{
	switch(code) {
		case ARCH_SET_GS:
			uk_pr_debug("arch_prctl option SET_GS(%p)\n",
				    (void *) addr);
			ukarch_sysregs_set_gs_base(&usc->sysregs, (__uptr)addr);
			return 0;

		case ARCH_SET_FS:
			uk_pr_debug("arch_prctl option SET_FS(%p)\n",
				    (void *) addr);
			ukarch_sysregs_set_tlsp(&usc->sysregs, (__uptr)addr);
			return 0;

		case ARCH_GET_GS: {
			uk_pr_debug("arch_prctl option GET_GS(%p)\n",
				    (void *) addr);
			if (!addr)
				return -EINVAL;
			*((long *)addr) =
				      ukarch_sysregs_get_gs_base(&usc->sysregs);
			return 0;
		}

		case ARCH_GET_FS: {
			uk_pr_debug("arch_prctl option GET_FS(%p)\n",
				    (void *) addr);
			if (!addr)
				return -EINVAL;
			*((long *)addr) =
					 ukarch_sysregs_get_tlsp(&usc->sysregs);
			return 0;
		}

		case ARCH_GET_CPUID:
			uk_pr_warn("arch_prctl option GET_CPUID not implemented\n");
			return -EINVAL;

		case ARCH_SET_CPUID:
			uk_pr_warn("arch_prctl option SET_CPUID not implemented\n");
			return -EINVAL;

		case ARCH_MAP_VDSO_X32:
			uk_pr_warn("arch_prctl option MAP_VDSO_X32 not implemented\n");
			return -EINVAL;

		case ARCH_MAP_VDSO_32:
			uk_pr_warn("arch_prctl option MAP_VDSO_32 not implemented\n");
			return -EINVAL;

		case ARCH_MAP_VDSO_64:
			uk_pr_warn("arch_prctl option MAP_VDSO_64 not implemented\n");
			return -EINVAL;
		default:
			break;
	}

	uk_pr_debug("arch_prctl option code 0x%lx ignored\n", code);
	return -EINVAL;
}

#if LIBC_SYSCALLS
int arch_prctl(int code, void *addr)
{
	return uk_syscall_e_arch_prctl((long) code, (long) addr, 0x0);
}
#endif
