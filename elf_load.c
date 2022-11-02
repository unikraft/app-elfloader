/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2017, Stefan Lankes, RWTH Aachen University
 * All rights reserved.
 * Copyright (c) 2018, Pierre Olivier, Virginia Polytechnic Institute and
 * State University. All rights reserved.
 * Copyright (c) 2019, Simon Kuenzer <simon.kuenzer@neclab.eu>,
 * NEC Laboratories Europe GmbH, NEC Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the University nor the names of its contributors
 *      may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Some code in this file was derived and adopted from:
 * HermiTux Kernel
 * https://github.com/ssrg-vt/hermitux-kernel, commit 2a3264d
 * File: tools/uhyve-elf.c
 * The authors of the original implementation are attributed in the license
 * header of this file. HermiTux kernel was released under BSD 3-clause
 * (see file /LICENSE in the HermiTux Kernel repository).
 */

#include <libelf.h>
#include <gelf.h>
#include <errno.h>
#include <string.h>
#include <uk/assert.h>
#include <uk/print.h>
#include <uk/essentials.h>
#include <uk/arch/limits.h>
#include <uk/errptr.h>
#if CONFIG_PAGING
#include <uk/plat/paging.h>
#include <uk/arch/limits.h>
#endif /* CONFIG_PAGING */

#include "libelf_helper.h"
#include "elf_prog.h"

struct elf_prog *elf_load_img(struct uk_alloc *a, void *img_base,
			      size_t img_len)
{
#if CONFIG_PAGING
	struct uk_pagetable *pt;
	int ret;
#endif /* CONFIG_PAGING */
	struct elf_prog *elf_prog = NULL;
	Elf *elf;
	GElf_Ehdr ehdr;
	GElf_Phdr phdr;
	size_t phnum, phi;
	uintptr_t prog_lowerl;
	uintptr_t prog_upperl;

	elf = elf_memory(img_base, img_len);
	if (!elf) {
		elferr_err("%p: Failed to initialize ELF parser\n",
			   img_base);
		errno = EBUSY;
		goto out;
	}

	if (elf_kind(elf) != ELF_K_ELF) {
		uk_pr_err("%p: Image format not recognized or not supported\n",
			  img_base);
		errno = EINVAL;
		goto out_free_elf;
	}

#if CONFIG_PAGING
	pt = ukplat_pt_get_active();
	if (PTRISERR(pt)) {
		uk_pr_warn("%p: Unable to set page protections bits. Continuing without...\n",
			   img_base);
		pt = NULL;
	}
#endif /* CONFIG_PAGING */

	/*
	 * Executable Header
	 */
	if (gelf_getehdr(elf, &ehdr) == NULL) {
		elferr_err("%p: Failed to get executable header",
			   img_base);
		errno = EINVAL;
		goto out_free_elf;
	}
	/* Check machine */
	uk_pr_debug("%p: ELF machine type: %"PRIu16"\n",
		    img_base, ehdr.e_machine);
	if
#if CONFIG_ARCH_X86_64
	(ehdr.e_machine != EM_X86_64)
#elif CONFIG_ARCH_ARM_64
	(ehdr.e_machine != EM_AARCH64)
#else
#error "Unsupported machine type"
#endif
	{
		uk_pr_err("%p: ELF machine type mismatch!\n", img_base);
		errno = EINVAL;
		goto out_free_elf;
	}
	/* Check ABI */
	uk_pr_debug("%p: ELF OS ABI: %"PRIu8"\n",
		    img_base, ehdr.e_ident[EI_OSABI]);
	if (ehdr.e_ident[EI_OSABI] != ELFOSABI_LINUX
	    && ehdr.e_ident[EI_OSABI] != ELFOSABI_NONE) {
		uk_pr_err("%p: ELF OS ABI unsupported!\n", img_base);
		errno = EINVAL;
		goto out_free_elf;
	}
	/* Check executable type */
	/* We support only static postiion-independent binaries for now:
	 * https://www.openwall.com/lists/musl/2015/06/01/12
	 * These binaries are type ET_DYN without having a PT_INTERP section */
	uk_pr_debug("%p: ELF object type: %"PRIu16"\n",
		    img_base, ehdr.e_type);
	if (ehdr.e_type != ET_DYN) {
		uk_pr_err("%p: ELF executable is not position-independent!\n",
			  img_base);
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
		elferr_err("%p: Failed to get number of program headers",
			   img_base);
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
			elferr_warn("%p: Failed to get program header %"PRIu64"\n",
				    img_base, (uint64_t) phi);
			continue;
		}

		if (phdr.p_type == PT_INTERP) {
			/* TODO: If our executable requests an interpreter
			 *       (e.g., dynamic loader) we have to stop since
			 *        we do not support it yet */
			uk_pr_err("%p: ELF executable requested program interpreter: Currently unsupported!\n",
				  img_base);
			errno = ENOTSUP;
			goto out_free_elf;
		}

		if (phdr.p_type != PT_LOAD) {
			/* We do not need to look further into headers
			 * that are not marked as 'load' */
			continue;
		}

		uk_pr_debug("%p: phdr[%"PRIu64"]: %c%c%c, offset: %p, vaddr: %p, paddr: %p, filesz: %"PRIu64" B, memsz %"PRIu64" B, align: %"PRIu64" B\n",
			    img_base, phi,
			    phdr.p_flags & PF_R ? 'R' : '-',
			    phdr.p_flags & PF_W ? 'W' : '-',
			    phdr.p_flags & PF_X ? 'X' : '-',
			    (void *) phdr.p_offset,
			    (void *) phdr.p_vaddr,
			    (void *) phdr.p_paddr,
			    (uint64_t) phdr.p_filesz,
			    (uint64_t) phdr.p_memsz,
			    (uint64_t) phdr.p_align);
		uk_pr_debug("%p: \\_ segment at pie + 0x%"PRIx64" (len: 0x%"PRIx64") from file @  0x%"PRIx64" (len: 0x%"PRIx64")\n",
			    img_base, phdr.p_paddr, phdr.p_memsz,
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

		uk_pr_debug("%p: \\_ base: pie + 0x%"PRIx64" (len: 0x%"PRIx64")\n",
			    img_base, prog_lowerl, prog_upperl - prog_lowerl);
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
		uk_pr_debug("%p: Not enough memory to load image (failed to allocate %"PRIu64" bytes)\n",
			    img_base, (uint64_t) elf_prog->img_len);
		uk_free(elf_prog->a, elf_prog);
		goto out_free_elf;
	}

	uk_pr_debug("%p: Program/Library memory region: 0x%"PRIx64"-0x%"PRIx64"\n",
		    img_base,
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


		if (!elf_prog->start || (phdr.p_paddr
				+ (uintptr_t) elf_prog->img < elf_prog->start))
			elf_prog->start = phdr.p_paddr
					  + (uintptr_t) elf_prog->img;

		uk_pr_debug("%p: Copying 0x%"PRIx64" - 0x%"PRIx64" -> 0x%"PRIx64" - 0x%"PRIx64"\n",
			    img_base,
			    (uint64_t) img_base + phdr.p_offset,
			    (uint64_t) img_base + phdr.p_offset + phdr.p_filesz,
			    (uint64_t) elf_prog->img + phdr.p_paddr,
			    (uint64_t) elf_prog->img + phdr.p_paddr
			     + phdr.p_filesz);
		memcpy((void *) elf_prog->img + phdr.p_paddr,
		       (const void *)((uintptr_t) img_base + phdr.p_offset),
		       (size_t) phdr.p_filesz);
		uk_pr_debug("%p: Zeroing 0x%"PRIx64" - 0x%"PRIx64"\n",
			    img_base,
			    (uint64_t) (elf_prog->img + phdr.p_paddr
			     + phdr.p_filesz),
			    (uint64_t) (elf_prog->img + phdr.p_paddr
			     + phdr.p_filesz + (phdr.p_memsz - phdr.p_filesz)));
		memset((void *)(elf_prog->img + phdr.p_paddr + phdr.p_filesz),
		       0, phdr.p_memsz - phdr.p_filesz);

#if CONFIG_PAGING
		/*
		 * Setup memory protection
		 */
		if (pt) {
			uk_pr_debug("%p: Protecting 0x%"PRIx64" - 0x%"PRIx64": %c%c%c\n",
				    img_base,
				    ALIGN_DOWN((uint64_t) (elf_prog->img
						+ phdr.p_paddr), __PAGE_SIZE),
				    ALIGN_UP((uint64_t) (elf_prog->img
						+ phdr.p_paddr + phdr.p_memsz),
						__PAGE_SIZE),
				    phdr.p_flags & PF_R ? 'R' : '-',
				    phdr.p_flags & PF_W ? 'W' : '-',
				    phdr.p_flags & PF_X ? 'X' : '-');
			ret = ukplat_page_set_attr(pt, ALIGN_DOWN((__vaddr_t)
				     elf_prog->img + phdr.p_paddr, __PAGE_SIZE),
				     DIV_ROUND_UP(phdr.p_memsz, __PAGE_SIZE),
				       ((phdr.p_flags & PF_R)
				       ? PAGE_ATTR_PROT_READ  : 0x0)
				     | ((phdr.p_flags & PF_W)
				       ? PAGE_ATTR_PROT_WRITE : 0x0)
				     | ((phdr.p_flags & PF_X)
				       ? PAGE_ATTR_PROT_EXEC  : 0x0),
				     0);
			if (ret < 0)
				uk_pr_err("%p: Failed to set protection bits: %d. Program execution with incoreect bits set may fail and/or is insecure.\n",
					  img_base, ret);
		}
#endif /* CONFIG_PAGING */
	}

out_free_elf:
	elf_end(elf);
out:
	return elf_prog;
}
