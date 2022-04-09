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

#ifndef ELF_PROG_H
#define ELF_PROG_H

#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include <uk/alloc.h>
#include <uk/arch/ctx.h>
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
 * Load an ELF program from a memory region. After loading,
 * the source image can be released.
 *
 * @param a:
 *   Reference to allocator for allocating space for program sections
 * @param img_base:
 *   Reference to ELF image in memory
 * @param img_len:
 *   Length of ELF image in memory
 * @return:
 *   On success an elf_prog instance is returned. Such a program
 *   can be prepared to be started with elf_ctx_init(). On errors,
 *   NULL is returned and `errno` is set accordingly.
 */
struct elf_prog *elf_load_img(struct uk_alloc *a, void *img_base,
			      size_t img_len);

/**
 * Initializes an ukarch_ctx with a loaded ELF program. This program
 * will be executed as soon as the context is scheduled/loaded to
 * the CPU. The function will populate the associated stack with
 * auxiliary vector, environment variables, and program arguments.
 *
 * @param ctx:
 *   uakrch_ctx to initialize, an associated stack is required
 * @param prog:
 *   Loaded ELF program (e.g., `elf_load_img()`)
 * @param argc:
 *   Argument count
 * @param argv:
 *   Argument vector
 *   NOTE: This function will copy the content of argv to the application stack
 * @param environ:
 *   NULL-terminated list of environment variables
 *   NOTE: This function will copy the content of environ to the application
 *         stack
 * @param rand:
 *   Random seed that is passed to the application
 */
void elf_ctx_init(struct ukarch_ctx *ctx, struct elf_prog *prog,
		  int argc, char *argv[], char *environ[],
		  uint64_t rand[2]);

#endif /* ELF_PROG_H */
