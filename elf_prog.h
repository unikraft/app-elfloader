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

#include <uk/config.h>
#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdbool.h>
#include <uk/alloc.h>
#include <uk/arch/ctx.h>
#include <uk/essentials.h>

struct elf_prog {
	struct uk_alloc *a;
	const char *name;
	const char *path; /* path to executable */
	void *vabase;	/* base address of loaded image in virtual memory */
	size_t valen;	/* length of loaded image in virtual memory */

	/* Needed by elf_ctx_init(): */
	uintptr_t start;
	uintptr_t entry;
	struct {
		uintptr_t off;
		size_t num;
		size_t entsize;
	} phdr;
	struct {
		bool required;
		char *path;
		struct elf_prog *prog;
	} interp;
	size_t align;
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
 * @param progname:
 *   Program name used for kernel messages, ideally matches with argv[0] of
 *   application that is loaded. Do not release/modify while the returned
 *   elf_prog is in use.
 * @return:
 *   On success an elf_prog instance is returned. Such a program
 *   can be prepared to be started with elf_ctx_init(). On errors,
 *   NULL is returned and `errno` is set accordingly.
 */
struct elf_prog *elf_load_img(struct uk_alloc *a, void *img_base,
			      size_t img_len, const char *progname);

#if CONFIG_LIBVFSCORE
/**
 * Load an ELF program from a file descriptor region. After loading,
 * the file descriptor can be closed.
 *
 * @param a:
 *   Reference to allocator for allocating space for program sections
 * @param path:
 *   VFS path to ELF executable file
 *   The actual c-strings should not be released/modified while the returned
 *   elf_prog is in use.
 * @param progname:
 *   Program name used for kernel messages, ideally matches with argv[0] of
 *   application that is loaded.
 *   The actual c-strings should not be released/modified while the returned
 *   elf_prog is in use.
 * @return:
 *   On success an elf_prog instance is returned. Such a program
 *   can be prepared to be started with elf_ctx_init(). On errors,
 *   NULL is returned and `errno` is set accordingly.
 */
struct elf_prog *elf_load_vfs(struct uk_alloc *a, const char *path,
			      const char *progname);
#endif /* CONFIG_LIBVFSCORE */

/**
 * Release a loaded ELF program
 * NOTE: This covers only the non-runtime resources, basically everything
 *       allocated by `elf_load_mem()` and `elf_load_vfs()`. Any further system
 *       state changes due to program execution (`elf_ctx_init()`) are not
 *       covered.
 */
void elf_unload(struct elf_prog *elf_prog);

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
 * @param argv0:
 *   Optional c-string that is prepended to the argument vector (see `argv`)
 *   for the application. If used, do not release/modify while the returned
 *   `ctx` and `prog` are in use.
 * @param argc:
 *   Argument count; can be only `0` if `argv0` is given, must be > 0 otherwise.
 * @param argv:
 *   Argument vector, only optional if `argv0` is given.
 *   This function will only copy the c-string references of argv to the
 *   application. The actual c-strings should not be released/modified while
 *   `ctx` and `prog` are in use.
 * @param environ:
 *   NULL-terminated list of environment variables
 *   This function will only copy the c-string references of environ to the
 *   application. The actual c-strings should not be released/modified while
 *   `ctx` and `prog` are in use.
 * @param rand:
 *   Random seed that is passed to the application
 *   Only a reference to rand[] is handed over to the application. Do not
 *   release/modify while `ctx` and `prog` are in use.
 */
void elf_ctx_init(struct ukarch_ctx *ctx, struct elf_prog *prog,
		  const char *argv0, int argc, char *argv[], char *environ[],
		  uint64_t rand[2]);

#endif /* ELF_PROG_H */
