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

#include <libelf.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <uk/essentials.h>
#include <uk/plat/memory.h>

#include "binfmt_elf.h"

#ifdef CONFIG_APPELFLOADER_BUILTIN
#include "elf_helloworld.h"
#endif

/*
 * Init libelf
 */
static __constructor void _libelf_init(void) {
	if (elf_version(EV_CURRENT) == EV_NONE)
		UK_CRASH("Failed to initialize libelf: Version error");
}

#ifdef CONFIG_APPELFLOADER_BUILTIN
int main(int argc, char *argv[])
{
	struct elf_prog *prog;
	int ret = 0;

	/*
	 * Parse image
	 */
	uk_pr_debug("Load built-in helloworld program image...\n");

	prog = load_elf(uk_alloc_get_default(), elf_helloworld, elf_helloworld_len, "helloworld");
	if (!prog) {
		ret = -errno;
		goto out;
	}

	/*
	 * Execute program
	 */
	uk_pr_debug("Execute image...\n");
	exec_elf(prog, argc, argv, NULL, 0xFEED, 0xC0FFEE);
	/* If we return here, the execution failed! :'( */
	ret = -EFAULT;

out:
	return ret;
}
#else
int main(int argc, char *argv[])
{
	struct ukplat_memregion_desc img;
	struct elf_prog *prog;
	int rc;
	int ret = 0;

	/*
	 * Make sure argv[0] exists
	 */
	if (argc <= 0 || !argv) {
		uk_pr_err("Program name missing (no argv[0])\n");
		ret = 1;
		goto out;
	}

	/*
	 * Find initrd
	 */
	uk_pr_debug("Searching for image...\n");
	rc = ukplat_memregion_find_initrd0(&img);
	if (rc < 0 || !img.base || !img.len) {
		uk_pr_err("No image found (initrd parameter missing?)\n");
		ret = 1;
		goto out;
	}
	uk_pr_info("Image at %p, len %"__PRIsz" bytes\n",
		   img.base, img.len);

	/*
	 * Parse image
	 */
	uk_pr_debug("Load image...\n");
	prog = load_elf(uk_alloc_get_default(), img.base, img.len, argv[0]);
	if (!prog) {
		ret = -errno;
		goto out;
	}

	/*
	 * Execute program
	 */
	uk_pr_debug("Execute image...\n");
	exec_elf(prog, argc, argv, NULL, 0xFEED, 0xC0FFEE);

	/* If we return here, the execution failed! :'( */

out:
	return ret;
}
#endif /* CONFIG_APPELFLOADER_BUILTIN */
