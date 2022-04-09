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

#include <uk/config.h>
#include <libelf.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <uk/essentials.h>
#include <uk/plat/memory.h>
#if CONFIG_LIBPOSIX_PROCESS
#include <uk/process.h>
#endif /* CONFIG_LIBPOSIX_PROCESS */
#include <uk/thread.h>
#include <uk/sched.h>

#include "elf_prog.h"

/*
 * Init libelf
 */
static __constructor void _libelf_init(void) {
	if (elf_version(EV_CURRENT) == EV_NONE)
		UK_CRASH("Failed to initialize libelf: Version error");
}

int main(int argc, char *argv[])
{
	struct ukplat_memregion_desc *img;
	struct elf_prog *prog;
	struct uk_thread *app_thread;
	uint64_t rand[2] = { 0xB0B0, 0xF00D }; /* FIXME: Use real random val */
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
	if (rc < 0 || !img->vbase || !img->len) {
		uk_pr_err("No image found (initrd parameter missing?)\n");
		ret = 1;
		goto out;
	}
	uk_pr_info("Image at %p, len %"__PRIsz" bytes\n",
		   (void *) img->vbase, img->len);

	/*
	 * Create thread container
	 * It will have a new stack and an ukarch_ctx
	 */
	app_thread = uk_thread_create_container(uk_alloc_get_default(),
						uk_alloc_get_default(), 0,
						uk_alloc_get_default(),
						false,
						"elfapp",
						NULL, NULL);
	if (!app_thread) {
		uk_pr_err("Failed to allocate thread container\n");
		ret = 1;
		goto out;
	}

	/*
	 * Parse image
	 */
	uk_pr_debug("Load image...\n");
	prog = elf_load_img(uk_alloc_get_default(), (void *) img->vbase,
			    img->len);
	if (!prog) {
		ret = -errno;
		goto out_free_thread;
	}
	uk_pr_info("ELF program loaded to 0x%"PRIx64"-0x%"PRIx64" (%"__PRIsz" B), entry at %p\n",
		   (uint64_t) prog->img, (uint64_t) prog->img + prog->img_len, prog->img_len,
		   (void *) prog->entry);

	/*
	 * Initialize application thread
	 */
	uk_pr_debug("Prepare application thread...\n");
	elf_ctx_init(&app_thread->ctx, prog,
		     argc, argv, NULL, rand);
	app_thread->flags |= UK_THREADF_RUNNABLE;
#if CONFIG_LIBPOSIX_PROCESS
	uk_posix_process_create(uk_alloc_get_default(),
				app_thread,
				uk_thread_current());
#endif

	/*
	 * Execute application
	 */
	uk_sched_thread_add(uk_sched_current(), app_thread);

	/*
	 * FIXME: Instead of an infinite wait, wait for application
	 *        to exit (this needs thread_wait support with
	 *        uksched and/or posix-process)
	 */
	for (;;)
		sleep(10);

	/* TODO: As soon as we are able to return: properly exit/shutdown */

out_free_thread:
	uk_thread_release(app_thread);
out:
	return ret;
}
