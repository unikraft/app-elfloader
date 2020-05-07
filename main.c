#include <libelf.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <uk/essentials.h>
#include <uk/plat/memory.h>

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
