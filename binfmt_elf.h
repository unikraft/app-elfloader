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

#ifndef BINFMT_ELF_H
#define BINFMT_ELF_H

#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include <uk/alloc.h>
#include <uk/essentials.h>

struct elf_prog {
	uintptr_t start;
	uintptr_t entry;
	uintptr_t ehdr_phoff;
	size_t ehdr_phnum;
	size_t ehdr_phentsize;

	struct uk_alloc *a;
	void *img;
	size_t img_len;
};

/**
 * @return 0 on success, *out is filled out
 */
struct elf_prog *load_elf(struct uk_alloc *a, void *img_base, size_t img_len,
			  const char *progname);

/**
 * Starts execution of *prog previosuly loaded by `load_elf()`
 * ///@return On success, the function does not return
 */
void exec_elf(struct elf_prog *prog,
	      int argc, char *argv[], char *environ[],
	      uint64_t rnd0, uint64_t rnd1) __noreturn;

#endif /* BINFMT_ELF_H */
