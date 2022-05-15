#include <uk/config.h>
#include <libelf.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <uk/essentials.h>
#include <uk/plat/memory.h>
#if CONFIG_LIBPOSIX_PROCESS
#include <uk/process.h>
#endif /* CONFIG_LIBPOSIX_PROCESS */
#include <uk/thread.h>
#include <uk/sched.h>

#include "binfmt_elf.h"

/*
 * Init libelf
 */
static __constructor void _libelf_init(void) {
	if (elf_version(EV_CURRENT) == EV_NONE)
		UK_CRASH("Failed to initialize libelf: Version error");
}

int main(int argc, char *argv[])
{
	struct ukplat_memregion_desc img;
	struct elf_prog *prog;
	struct uk_thread *main_thread;
	uint64_t rand[] = { 0xFEED, 0xC0FFEE };
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
	 * Create thread
	 */
	main_thread = uk_thread_create_container(uk_alloc_get_default(),
						 uk_alloc_get_default(), 0,
						 uk_alloc_get_default(),
						 false,
						 "elfapp",
						 NULL, NULL);
	if (!main_thread) {
		uk_pr_err("Failed to allocate thread container\n");
		ret = 1;
		goto out;
	}

	/*
	 * Parse image
	 */
	uk_pr_debug("Load image...\n");
	prog = load_elf(uk_alloc_get_default(), img.base, img.len, argv[0]);
	if (!prog) {
		ret = -errno;
		goto out_free_thread;
	}

	/*
	 * Initialize main thread
	 */
	uk_pr_debug("Prepare main thread...\n");
	ctx_elf(&main_thread->ctx, prog,
		argc, argv, NULL, (uint64_t **)rand);
	main_thread->flags |= UK_THREADF_RUNNABLE;
#if CONFIG_LIBPOSIX_PROCESS
	uk_posix_process_create(uk_alloc_get_default(),
				main_thread,
				uk_thread_current());
#endif
	/*
	 * Execute program
	 */
	uk_sched_thread_add(uk_sched_current(), main_thread);

	for(;;)
		sleep(10);

	/* If we return here, the execution failed! :'( */

out_free_thread:
	uk_thread_release(main_thread);
out:
	return ret;
}
