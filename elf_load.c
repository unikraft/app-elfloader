#include <libelf.h>
#include <gelf.h>
#include <errno.h>
#include <string.h>
#include <uk/assert.h>
#include <uk/print.h>
#include <uk/essentials.h>
#include <uk/arch/limits.h>

#include "libelf_helper.h"
#include "binfmt_elf.h"

struct elf_prog *load_elf(struct uk_alloc *a, void *img_base, size_t img_len,
			  const char *progname)
{
	struct elf_prog *elf_prog = NULL;
	Elf *elf;
	GElf_Ehdr ehdr;
	GElf_Phdr phdr;
	size_t phnum, phi;
	uintptr_t prog_lowerl;
	uintptr_t prog_upperl;

	elf = elf_memory(img_base, img_len);
	if (!elf) {
		elferr_err("%s: Failed to initialize ELF parser\n",
			   progname);
		errno = EBUSY;
		goto out;
	}

	if (elf_kind(elf) != ELF_K_ELF) {
		uk_pr_err("%s: Image format not recognized or not supported\n",
			  progname);
		errno = EINVAL;
		goto out_free_elf;
	}

	/*
	 * Executable Header
	 */
	if (gelf_getehdr(elf, &ehdr) == NULL) {
		elferr_err("%s: Failed to get executable header",
			   progname);
		errno = EINVAL;
		goto out_free_elf;
	}
	/* Check machine */
	uk_pr_debug("%s: ELF machine type: %"PRIu16"\n",
		    progname, ehdr.e_machine);
	if
#if CONFIG_ARCH_X86_64
	(ehdr.e_machine != EM_X86_64)
#elif CONFIG_ARCH_ARM_64
	(ehdr.e_machine != EM_AARCH64)
#else
#error "Unsupported machine type"
#endif
	{
		uk_pr_err("%s: ELF machine type mismatch!\n", progname);
		errno = EINVAL;
		goto out_free_elf;
	}
	/* Check ABI */
	uk_pr_debug("%s: ELF OS ABI: %"PRIu8"\n",
		    progname, ehdr.e_ident[EI_OSABI]);
	if (ehdr.e_ident[EI_OSABI] != ELFOSABI_LINUX
	    && ehdr.e_ident[EI_OSABI] != ELFOSABI_NONE) {
		uk_pr_err("%s: ELF OS ABI unsupported!\n", progname);
		errno = EINVAL;
		goto out_free_elf;
	}
	/* Check executable type */
	/* We support only static postiion-independent binaries for now:
	 * https://www.openwall.com/lists/musl/2015/06/01/12
	 * These binaries are type ET_DYN without having a PT_INTERP section */
	uk_pr_debug("%s: ELF object type: %"PRIu16"\n",
		    progname, ehdr.e_type);
	if (ehdr.e_type != ET_DYN) {
		uk_pr_err("%s: ELF executable is not position-independent!\n",
			  progname);
		errno = EINVAL;
		goto out_free_elf;
	}

	/*
	 * Scan program headers
	 */
	/* When we have ET_DYN we shoiuld check that there is no PT_INTERP
	 * section; otherwise we need to support dynamic loading.
	 * For this purpose we need to scan the phdrs */
	if (elf_getphnum(elf, &phnum) == 0) {
		elferr_err("%s: Failed to get number of program headers",
			   progname);
		errno = EINVAL;
		goto out_free_elf;
	}

	/* While checking for compatible headers, we are trying to figure out
	 * how much memory needs to be allocated for loading program. We start
	 * loading the according segments in a second step */
	prog_lowerl = 0;
	prog_upperl = 0;
	for (phi = 0; phi < phnum; ++phi) {
		if (gelf_getphdr(elf, phi, &phdr) != &phdr) {
			elferr_warn("%s: Failed to get program header %"PRIu64"\n",
				    progname, (uint64_t) phi);
			continue;
		}

		if (phdr.p_type == PT_INTERP) {
			/* TODO: If our executable requests an interpreter
			 *       (e.g., dynamic loader) we have to stop since
			 *        we do not support it yet */
			uk_pr_err("%s: ELF executable requested program interpreter: Currently unsupported!\n",
				  progname);
			errno = ENOTSUP;
			goto out_free_elf;
		}

		if (phdr.p_type != PT_LOAD) {
			/* We do not need to look further into headers
			 * that are not marked as 'load' */
			continue;
		}

		uk_pr_debug("%s: phdr[%"PRIu64"]: %c%c%c, offset: %p, vaddr: %p, paddr: %p, filesz: %"PRIu64" B, memsz %"PRIu64" B, align: %"PRIu64" B\n",
			    progname, phi,
			    phdr.p_flags & PF_R ? 'R' : '-',
			    phdr.p_flags & PF_W ? 'W' : '-',
			    phdr.p_flags & PF_X ? 'X' : '-',
			    (void *) phdr.p_offset,
			    (void *) phdr.p_vaddr,
			    (void *) phdr.p_paddr,
			    (uint64_t) phdr.p_filesz,
			    (uint64_t) phdr.p_memsz,
			    (uint64_t) phdr.p_align);
		uk_pr_debug("%s: \\_ segment at pie + 0x%"PRIx64" (len: 0x%"PRIx64") from file @  0x%"PRIx64" (len: 0x%"PRIx64")\n",
			    progname, phdr.p_paddr, phdr.p_memsz,
			    (uint64_t) phdr.p_offset, (uint64_t) phdr.p_filesz);

		if (prog_lowerl == 0 && prog_upperl == 0) {
			/* first run */
			prog_lowerl = phdr.p_paddr;
			prog_upperl = prog_lowerl + phdr.p_memsz;
		} else {
			/* Move lower and upper border */
			if (phdr.p_paddr < prog_lowerl)
				prog_lowerl = phdr.p_paddr;
			if (phdr.p_paddr + phdr.p_memsz > prog_upperl)
				prog_upperl = phdr.p_paddr + phdr.p_memsz;
		}
		UK_ASSERT(prog_lowerl <= prog_upperl);

		uk_pr_debug("%s: \\_ base: pie + 0x%"PRIx64" (len: 0x%"PRIx64")\n",
			    progname, prog_lowerl, prog_upperl - prog_lowerl);
	}

	/*
	 * At this point we are done with checking the image.
	 * We will allocate memory for `struct elf_prog` and for the
	 * program's/library's address space. We use a single contiguous
	 * allocation for it, for now
	 */
	/* We do not support yet an img base other than 0 */
	UK_ASSERT(prog_lowerl == 0);

	elf_prog = uk_calloc(a, 1, sizeof(*elf_prog));
	if (!elf_prog)
		goto out_free_elf;

	elf_prog->a = a;
	elf_prog->img_len = (size_t) (prog_upperl - prog_lowerl);
	elf_prog->img = uk_memalign(elf_prog->a, __PAGE_SIZE,
				    elf_prog->img_len);
	if (!elf_prog->img) {
		uk_pr_debug("%s: Not enough memory to load image (failed to allocate %"PRIu64" bytes)\n",
			    progname, (uint64_t) elf_prog->img_len);
		uk_free(elf_prog->a, elf_prog);
		goto out_free_elf;
	}

	uk_pr_debug("%s: Program/Library memory region: 0x%"PRIx64"-0x%"PRIx64"\n",
		    progname,
		    (uint64_t) elf_prog->img,
		    (uint64_t) elf_prog->img + elf_prog->img_len);

	/* Fill-out elf_prog and load segments to allocated memory */
	elf_prog->ehdr_phoff = ehdr.e_phoff;
	elf_prog->ehdr_phnum = ehdr.e_phnum;
	elf_prog->ehdr_phentsize = ehdr.e_phentsize;
	elf_prog->entry = ehdr.e_entry + (uintptr_t) elf_prog->img;
	for (phi = 0; phi < phnum; ++phi) {
		if ((gelf_getphdr(elf, phi, &phdr) != &phdr)
		    || (phdr.p_type != PT_LOAD)) {
			continue;
		}


		if(!elf_prog->start || (phdr.p_paddr + (uintptr_t) elf_prog->img < elf_prog->start))
			elf_prog->start = phdr.p_paddr + (uintptr_t) elf_prog->img;

		uk_pr_debug("%s: Copying 0x%"PRIx64" - 0x%"PRIx64" -> 0x%"PRIx64" - 0x%"PRIx64"\n",
			    progname,
			    (uint64_t) img_base + phdr.p_offset,
			    (uint64_t) img_base + phdr.p_offset + phdr.p_filesz,
			    (uint64_t) elf_prog->img + phdr.p_paddr,
			    (uint64_t) elf_prog->img + phdr.p_paddr + phdr.p_filesz);
		memcpy((void *) elf_prog->img + phdr.p_paddr,
		       (const void *)((uintptr_t) img_base + phdr.p_offset),
		       (size_t) phdr.p_filesz);
		uk_pr_debug("%s: Zeroing 0x%"PRIx64" - 0x%"PRIx64"\n",
			    progname,
			    (uint64_t) (elf_prog->img + phdr.p_paddr + phdr.p_filesz),
			    (uint64_t) (elf_prog->img + phdr.p_paddr + phdr.p_filesz + (phdr.p_memsz - phdr.p_filesz)));
		memset((void *)(elf_prog->img + phdr.p_paddr + phdr.p_filesz),
		       0, phdr.p_memsz - phdr.p_filesz);

		/*
		 * TODO: Setup memory protection (e.g., ukplat_mprotect())
		 */
	}

out_free_elf:
	elf_end(elf);
out:
	return elf_prog;
}
