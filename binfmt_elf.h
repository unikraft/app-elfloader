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
void ctx_elf(struct ukarch_ctx *ctx, struct elf_prog *prog,
		     int argc, char *argv[], char *environ[],
		     uint64_t *rand[2]);

#endif /* BINFMT_ELF_H */
