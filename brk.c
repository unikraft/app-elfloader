/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Authors: Simon Kuenzer <simon.kuenzer@neclab.eu>
 *
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

#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <uk/alloc.h>
#include <uk/print.h>
#include <uk/syscall.h>
#include <uk/arch/limits.h>

#ifndef PAGES2BYTES
#define PAGES2BYTES(x) ((x) << __PAGE_SHIFT)
#endif

#define HEAP_PAGES CONFIG_APPELFLOADER_BRK_NBPAGES
#define HEAP_LEN   PAGES2BYTES(CONFIG_APPELFLOADER_BRK_NBPAGES)

/*
 * For now we only support one custom stack
 */
static void *base = NULL;
static void *zeroed = NULL;
static intptr_t len = 0;

UK_LLSYSCALL_R_DEFINE(void *, brk, void *, addr)
{
	if (!addr && base) {
		/* another brk context request although we have one already */
		uk_pr_crit("Cannot handle multiple user space heaps: Not implemented!\n");
		return ERR2PTR(-ENOMEM);
	}

	if (!addr) {
		/* allocate brk context */
		base = uk_palloc(uk_alloc_get_default(), HEAP_PAGES);
		if (!base) {
			uk_pr_crit("Could not allocate memory for heap (%"PRIu64" KiB): Out of memory\n",
				   (uint64_t) HEAP_LEN / 1024);
			return ERR2PTR(-ENOMEM);
		}
		zeroed = base;
		addr = base;
	}

	if (addr < base || addr >= (base + HEAP_LEN)) {
		uk_pr_crit("Failed to increase heap: Not implemented!\n");
		return ERR2PTR(-ENOMEM);
	}

	/* Zero out requested memory (e.g., glibc requires) */
	if (addr > zeroed) {
		uk_pr_debug("zeroing %p-%p...\n", zeroed, addr);
		memset(zeroed, 0x0, (size_t) (addr - zeroed));
	}
	zeroed = addr;

	len = addr - base;
	uk_pr_debug("brk @ %p (brk heap region: %p-%p)\n", addr, base, base + HEAP_LEN);
	return addr;
}

#if LIBC_SYSCALLS
#include <unistd.h>
#include <uk/errptr.h>

int brk(void *addr)
{
	long ret;
	ret = uk_syscall_r_brk(addr);
	if (ret == 0) {
		errno = EFAULT;
		return -1;
	}
	if (PTRISERR(ret)) {
		errno = PTR2ERR(ret);
		return -1;
	}
	return 0;
}

void *sbrk(intptr_t inc)
{
	long ret;
	void *prev_base = base;

	if (!base) {
		/* Case when we do not have any memory allocated yet */
		if (inc > HEAP_LEN) {
			errno = ENOMEM;
			return (void *) -1;
		}
		ret = uk_syscall_r_brk(NULL);
	} else {
		/* We are increasing or reducing our range */
		ret = uk_syscall_r_brk((long)base + len + inc);
	}

	if (ret == 0) {
		errno = EFAULT;
		return (void *) -1;
	}
	if (PTRISERR(ret)) {
		errno = PTR2ERR(ret);
		return (void *) -1;
	}

	if (!prev_base)
		return base;
	return prev_base;
}
#endif /* LIBC_SYSCALLS */
