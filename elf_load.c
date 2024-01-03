/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2017, Stefan Lankes, RWTH Aachen University
 * All rights reserved.
 * Copyright (c) 2018, Pierre Olivier, Virginia Polytechnic Institute and
 * State University. All rights reserved.
 * Copyright (c) 2019, Simon Kuenzer <simon.kuenzer@neclab.eu>,
 * NEC Laboratories Europe GmbH, NEC Corporation. All rights reserved.
 * Copyright (c) 2023, Simon Kuenzer <simon@unikraft.io>,
 * Unikraft GmbH. All rights reserved.
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

#include <uk/config.h>

#include <libelf.h>
#include <gelf.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#if CONFIG_LIBVFSCORE
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif /* CONFIG_LIBVFSCORE */
#include <uk/assert.h>
#include <uk/print.h>
#include <uk/essentials.h>
#include <uk/arch/limits.h>
#include <uk/errptr.h>
#if CONFIG_LIBUKVMEM
#include <uk/vmem.h>
#include <uk/arch/limits.h>
#endif /* CONFIG_LIBUKVMEM */
#include <sys/mman.h>

#include "libelf_helper.h"
#include "elf_prog.h"

/*
 * Checks that ELF headers are valid and supported and
 * computes the size of needed virtual memory space for the image
 */
static int elf_load_parse(struct elf_prog *elf_prog, Elf *elf)
{
	GElf_Ehdr ehdr;
	GElf_Phdr phdr;
	size_t phi;
	int ret;

	UK_ASSERT(elf_prog);
	UK_ASSERT(elf);

	if (unlikely(elf_kind(elf) != ELF_K_ELF)) {
		uk_pr_err("%s: Image format not recognized or not supported\n",
			  elf_prog->name);
		ret = -ENOEXEC;
		goto err_out;
	}

	/*
	 * Executable Header
	 */
	if (unlikely(gelf_getehdr(elf, &ehdr) == NULL)) {
		elferr_err("%s: Failed to get executable header",
			   elf_prog->name);
		ret = -ENOEXEC;
		goto err_out;
	}
	/* Check machine */
	uk_pr_debug("%s: ELF machine type: %"PRIu16"\n",
		    elf_prog->name, ehdr.e_machine);
	if
#if CONFIG_ARCH_X86_64
	unlikely((ehdr.e_machine != EM_X86_64))
#elif CONFIG_ARCH_ARM_64
	unlikely((ehdr.e_machine != EM_AARCH64))
#else
#error "Unsupported machine type"
#endif
	{
		uk_pr_err("%s: ELF machine type mismatch!\n", elf_prog->name);
		ret = -ENOEXEC;
		goto err_out;
	}
	/* Check ABI */
	uk_pr_debug("%s: ELF OS ABI: %"PRIu8"\n",
		    elf_prog->name, ehdr.e_ident[EI_OSABI]);
	if (unlikely(ehdr.e_ident[EI_OSABI] != ELFOSABI_LINUX &&
	    ehdr.e_ident[EI_OSABI] != ELFOSABI_NONE)) {
		uk_pr_err("%s: ELF OS ABI unsupported: Require ELFOSABI_LINUX\n",
			  elf_prog->name);
		ret = -ENOEXEC;
		goto err_out;
	}
	/* Executable type
	 * NOTE: We support only position-independent binaries for now:
	 * https://www.openwall.com/lists/musl/2015/06/01/12
	 * These binaries are type ET_DYN
	 */
	uk_pr_debug("%s: ELF object type: %"PRIu16"\n",
		    elf_prog->name, ehdr.e_type);
	if (unlikely(ehdr.e_type != ET_DYN)) {
		uk_pr_err("%s: ELF executable is not position-independent!\n",
			  elf_prog->name);
		ret = -ENOEXEC;
		goto err_out;
	}

	/*
	 * Scan program headers
	 *
	 * While checking for compatible headers, we are also figuring out
	 * how much virtual memory needs to be reserved for loading the program.
	 */
	for (phi = 0; phi < ehdr.e_phnum; ++phi) {
		if (gelf_getphdr(elf, phi, &phdr) != &phdr) {
			elferr_warn("%s: Failed to get program header %"PRIu64"\n",
				    elf_prog->name, (uint64_t) phi);
			continue;
		}

		if (phdr.p_type == PT_INTERP) {
			if (elf_prog->interp.required) {
				uk_pr_err("%s: ELF executable requests multiple program interpreters: Unsupported\n",
					  elf_prog->name);
				ret = -ENOTSUP;
				goto err_out;
			}
			elf_prog->interp.required = true;
			continue;
		}

		if (phdr.p_type != PT_LOAD) {
			/* We do not need to look further into headers
			 * that are not marked as 'load'
			 */
			continue;
		}

		if (elf_prog->align < phdr.p_align)
			elf_prog->align = phdr.p_align;

		uk_pr_debug("%s: phdr[%"PRIu64"]: %c%c%c, offset: %p, vaddr: %p, paddr: %p, filesz: %"PRIu64" B, memsz %"PRIu64" B, align: %"PRIu64" B\n",
			    elf_prog->name, phi,
			    phdr.p_flags & PF_R ? 'R' : '-',
			    phdr.p_flags & PF_W ? 'W' : '-',
			    phdr.p_flags & PF_X ? 'X' : '-',
			    (void *) phdr.p_offset,
			    (void *) phdr.p_vaddr,
			    (void *) phdr.p_paddr,
			    (uint64_t) phdr.p_filesz,
			    (uint64_t) phdr.p_memsz,
			    (uint64_t) phdr.p_align);
		uk_pr_debug("%s: \\_ segment at pie + 0x%"PRIx64" (len: 0x%"PRIx64") from file @ 0x%"PRIx64" (len: 0x%"PRIx64")\n",
			    elf_prog->name, phdr.p_paddr, phdr.p_memsz,
			    (uint64_t) phdr.p_offset, (uint64_t) phdr.p_filesz);

		if (elf_prog->lowerl == 0 && elf_prog->upperl == 0) {
			/* first run */
			elf_prog->lowerl = phdr.p_paddr;
			elf_prog->upperl = elf_prog->lowerl + phdr.p_memsz;
		} else {
			/* Move lower and upper border */
			if (phdr.p_paddr < elf_prog->lowerl)
				elf_prog->lowerl = phdr.p_paddr;
			if (phdr.p_paddr + phdr.p_memsz > elf_prog->upperl)
				elf_prog->upperl = phdr.p_paddr + phdr.p_memsz;
		}
		UK_ASSERT(elf_prog->lowerl <= elf_prog->upperl);

		/* Calculate the in-memory phdr offset */
		if (phdr.p_offset <= ehdr.e_phoff &&
		    ehdr.e_phoff < phdr.p_offset + phdr.p_filesz)
			elf_prog->phdr.off = ehdr.e_phoff - phdr.p_offset +
					     phdr.p_paddr;
	}
	uk_pr_debug("%s: base: pie + 0x%"PRIx64", len: 0x%"PRIx64"\n",
		    elf_prog->name, elf_prog->lowerl, elf_prog->upperl - elf_prog->lowerl);

	/* This should've been set a few lines above. It can't be 0 either
	 * because it would overlap with the actual Elf Header.
	 */
	UK_ASSERT(elf_prog->phdr.off);

	elf_prog->phdr.num = ehdr.e_phnum;
	elf_prog->phdr.entsize = ehdr.e_phentsize;
	elf_prog->valen = PAGE_ALIGN_UP(elf_prog->upperl);
	return 0;

err_out:
	return ret;
}

#if CONFIG_LIBPOSIX_MMAP
static void elf_unload_vaimg(struct elf_prog *elf_prog)
{
	int rc;

	if (elf_prog->vabase) {
		rc = munmap(elf_prog->vabase, elf_prog->valen);
		if (unlikely(rc))
			uk_pr_err("Failed to munmap %s\n", elf_prog->name);

		elf_prog->vabase = NULL;
		elf_prog->start = 0;
		elf_prog->entry = 0;
	}
}
#else /* !CONFIG_LIBPOSIX_MMAP */
static void elf_unload_vaimg(struct elf_prog *elf_prog)
{
	if (elf_prog->vabase) {
		uk_free(elf_prog->a, elf_prog->vabase);
		elf_prog->vabase = NULL;
		elf_prog->start = 0;
		elf_prog->entry = 0;
	}
}
#endif /* !CONFIG_LIBPOSIX_MMAP */

static int elf_load_imgcpy(struct elf_prog *elf_prog, Elf *elf,
			   const void *img_base, size_t img_len __unused)
{
	size_t phnum, phi;
	uintptr_t vastart;
	uintptr_t vaend;
	GElf_Ehdr ehdr;
	GElf_Phdr phdr;
	int ret;

	UK_ASSERT(elf_prog->align && PAGE_ALIGNED(elf_prog->align));

	if (unlikely(gelf_getehdr(elf, &ehdr) == NULL)) {
		elferr_err("%s: Failed to get executable header",
			   elf_prog->name);
		ret = -ENOEXEC;
		goto err_out;
	}

	elf_prog->vabase = uk_memalign(elf_prog->a, elf_prog->align,
				       elf_prog->valen);
	if (unlikely(!elf_prog->vabase)) {
		uk_pr_debug("%s: Not enough memory to load image (failed to allocate %"PRIu64" bytes)\n",
			    elf_prog->name, (uint64_t) elf_prog->valen);
		return -ENOMEM;
	}

	uk_pr_debug("%s: Program/Library memory region: 0x%"PRIx64"-0x%"PRIx64"\n",
		    elf_prog->name,
		    (uint64_t) elf_prog->vabase,
		    (uint64_t) elf_prog->vabase + elf_prog->valen);

	/* Load segments to allocated memory and set start & entry */
	if (unlikely(elf_getphnum(elf, &phnum) == 0)) {
		elferr_err("%s: Failed to get number of program headers",
			   elf_prog->name);
		ret = -ENOEXEC;
		goto err_free_img;
	}

	elf_prog->entry = (uintptr_t) elf_prog->vabase + ehdr.e_entry;
	for (phi = 0; phi < phnum; ++phi) {
		if (gelf_getphdr(elf, phi, &phdr) != &phdr) {
			elferr_warn("%s: Failed to get program header %"PRIu64"\n",
				    elf_prog->name, (uint64_t) phi);
			continue;
		}
		if (phdr.p_type != PT_LOAD)
			continue;

		vastart = phdr.p_paddr + (uintptr_t) elf_prog->vabase;
		vaend   = vastart + phdr.p_filesz;
		if (!elf_prog->start || (vastart < elf_prog->start))
			elf_prog->start = vastart;

		uk_pr_debug("%s: Copying 0x%"PRIx64" - 0x%"PRIx64" -> 0x%"PRIx64" - 0x%"PRIx64"\n",
			    elf_prog->name,
			    (uint64_t) img_base + phdr.p_offset,
			    (uint64_t) img_base + phdr.p_offset + phdr.p_filesz,
			    (uint64_t) vastart,
			    (uint64_t) vaend);
		memcpy((void *) vastart,
		       (const void *)((uintptr_t) img_base + phdr.p_offset),
		       (size_t) phdr.p_filesz);

		/* Compute the area that needs to be zeroed */
		vastart = vaend;
		vaend   = vastart + (phdr.p_memsz - phdr.p_filesz);
		vaend   = PAGE_ALIGN_UP(vaend);
		uk_pr_debug("%s: Zeroing 0x%"PRIx64" - 0x%"PRIx64"\n",
			    elf_prog->name,
			    (uint64_t) (vastart),
			    (uint64_t) (vaend));
		memset((void *)(vastart), 0, vaend - vastart);
	}
	return 0;

err_free_img:
	elf_unload_vaimg(elf_prog);
err_out:
	return ret;
}

#if CONFIG_LIBVFSCORE
#if CONFIG_LIBPOSIX_MMAP
/* If vastart + phdr.p_filesz (vastart) < vastart + phdr.p_memsz (vaend),
 * 0 out that remainder, either through memset or through anonymous mappings
 */
static int elf_load_mmap_filesz_memsz_diff(struct elf_prog *elf_prog,
					   GElf_Phdr *phdr __maybe_unused,
					   uintptr_t vastart, uintptr_t vaend)
{
	UK_ASSERT(elf_prog && phdr && vastart && vaend);

	uk_pr_debug("%s: Zeroing 0x%"PRIx64" - 0x%"PRIx64"\n",
		    elf_prog->name,
		    (uint64_t)(vastart),
		    (uint64_t)(vaend));

	/* From the last byte contained in filesz to the last
	 * byte contained in memsz, we can either have:
	 * 1. a page alignment adjustment, where the end
	 * address is just PAGE_ALIGN_UP(paddr + filesz), which
	 * means that our initial call to mmap already read in
	 * a page for us whose remaining bytes we memset without
	 * generating a page fault, because
	 * vaend - vastart < PAGE_SIZE
	 * ...
	 */
	memset((void *)vastart, 0, PAGE_ALIGN_UP(vastart) - vastart);

	if (vaend == PAGE_ALIGN_UP(vastart))
		return 0;

	/*
	 * ...
	 * 2. vaend - vastart >= PAGE_SIZE, which can happen if
	 * this segment contains a NOBITS section, such as .bss.
	 * If that is the case, then simply anonymously map this
	 * remaining area so that we don't waste time memsetting
	 * it (.bss is quite large).
	 */
	vastart = PAGE_ALIGN_UP(vastart);
	vastart = (uintptr_t)mmap((void *)vastart, vaend - vastart,
				  PROT_EXEC | PROT_READ | PROT_WRITE,
				  MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
				  -1, 0);
	if (unlikely(vastart == (uintptr_t)MAP_FAILED)) {
		uk_pr_err("Failed to mmap the NOBITS part of phdr at "
			  "offset %lu\n", phdr->p_offset);
		return (int)vastart;
	}

	return 0;
}

/* Use this to mmap first PT_LOAD */
static int do_elf_load_fdphdr_0(struct elf_prog *elf_prog,
				GElf_Phdr *phdr, int fd)
{
	uintptr_t vastart, vaend;
	__sz mmap_len;
	int rc;

	/* First start/vabase can't be !0 before loading first PT_LOAD */
	UK_ASSERT(!elf_prog->start && !elf_prog->vabase && phdr);
	/* PT_LOAD p_vaddr/p_paddr must be multiple of page size */
	UK_ASSERT(!(phdr->p_vaddr & (phdr->p_align - 1)));
	UK_ASSERT(!(phdr->p_paddr & (phdr->p_align - 1)));

	mmap_len = elf_prog->valen + elf_prog->align;

	/* Do a first dummy mmap to pre-allocate a large enough contiguous
	 * mapping.
	 */
	vastart = (uintptr_t)mmap(NULL, mmap_len,
				  PROT_EXEC | PROT_READ | PROT_WRITE,
				  MAP_PRIVATE | MAP_ANONYMOUS,
				  -1, 0);
	if (unlikely(vastart == (uintptr_t)MAP_FAILED)) {
		uk_pr_err("Failed to mmap dummy area\n");
		return (int)vastart;
	}

	/* Now unmap dummy area, since we've fetched the base address of the
	 * large enough memory area
	 */
	rc = munmap((void *)vastart, mmap_len);
	if (unlikely(rc)) {
		uk_pr_err("Failed to unmap dummy area\n");
		return rc;
	}

	vastart = ALIGN_UP(vastart, elf_prog->align);
	elf_prog->vabase = (void *)vastart;
	/* We got ehdr.e_entry added initially at the start of elf_load_fd() */
	elf_prog->entry += (uintptr_t)elf_prog->vabase;

	uk_pr_debug("%s: Program/Library memory region: 0x%"PRIx64"-0x%"PRIx64"\n",
		    elf_prog->name,
		    (uint64_t)elf_prog->vabase,
		    (uint64_t)elf_prog->vabase + elf_prog->valen);


	vastart = (uintptr_t)mmap((void *)vastart + phdr->p_paddr,
				  phdr->p_filesz,
				  PROT_EXEC | PROT_READ | PROT_WRITE,
				  MAP_PRIVATE | MAP_FIXED,
				  fd, phdr->p_offset);
	if (unlikely(vastart == (uintptr_t)MAP_FAILED)) {
		uk_pr_err("Failed to mmap first phdr\n");
		return (int)vastart;
	}
	elf_prog->start = vastart;

	uk_pr_debug("%s: Memory mapped 0x%"PRIx64" - 0x%"PRIx64" to 0x%"PRIx64" - 0x%"PRIx64"\n",
		    elf_prog->name,
		    (uint64_t)phdr->p_offset,
		    (uint64_t)phdr->p_offset + phdr->p_filesz,
		    (uint64_t)vastart,
		    (uint64_t)vastart + (uint64_t)phdr->p_filesz);

	/* mmap anonymously what we are left if memsz > filesz */
	vastart += phdr->p_filesz;
	vaend = PAGE_ALIGN_UP(vastart + (phdr->p_memsz - phdr->p_filesz));
	if (vaend > vastart) {
		rc = elf_load_mmap_filesz_memsz_diff(elf_prog, phdr,
						     vastart, vaend);
		if (unlikely(rc)) {
			uk_pr_err("Failed to map difference between filesz and "
				  "memsz\n");
			return rc;
		}
	}

	return 0;
}

/* Use this to mmap every PT_LOAD but the first one */
static int do_elf_load_fdphdr_not0(struct elf_prog *elf_prog,
				   GElf_Phdr *phdr, int fd)
{
	uintptr_t vastart, vaend;
	uint64_t delta_p_offset;
	void *addr;
	int rc;

	/* If this is not the first PT_LOAD then vabase/start must be != 0 */
	UK_ASSERT(elf_prog->vabase && elf_prog->start && phdr);

	/* PT_LOAD's paddr may be misaligned, so keep in mind the
	 * offset from what should have been the aligned paddr to
	 * what we ended up with.
	 * As per ELF spec, the file offset and virtual/physical address
	 * are congruent modulo alignment, which must be multiple of page size.
	 * Therefore, taking care of the address misalignment will also take
	 * care of the file misalignment.
	 */
	delta_p_offset = phdr->p_paddr - PAGE_ALIGN_DOWN(phdr->p_paddr);

	addr = (void *)PAGE_ALIGN_DOWN((phdr->p_paddr +
				       (uintptr_t)elf_prog->vabase));

	uk_pr_debug("%s: Memory mapping 0x%"PRIx64" - 0x%"PRIx64" to 0x%"PRIx64" - 0x%"PRIx64"\n",
		    elf_prog->name,
		    (uint64_t)phdr->p_offset - delta_p_offset,
		    (uint64_t)phdr->p_offset + phdr->p_filesz + delta_p_offset,
		    (uint64_t)addr,
		    (uint64_t)addr + (uint64_t)phdr->p_filesz + delta_p_offset);

	/* mmap with all flags. If protections are enabled, these will
	 * be manually re-adjusted later.
	 */
	vastart = (uintptr_t)mmap(addr, phdr->p_filesz + delta_p_offset,
				  PROT_EXEC | PROT_READ | PROT_WRITE,
				  MAP_FIXED | MAP_PRIVATE,
				  fd, phdr->p_offset - delta_p_offset);
	if (unlikely(vastart == (uintptr_t)MAP_FAILED)) {
		uk_pr_err("Failed to mmap the phdr at offset %lu\n",
			  phdr->p_offset);
		return (int)vastart;
	}

	/* mmap anonymously what we are left if memsz > filesz */
	vastart += phdr->p_filesz + delta_p_offset;
	vaend = PAGE_ALIGN_UP(vastart + (phdr->p_memsz - phdr->p_filesz));
	if (vaend > vastart) {
		rc = elf_load_mmap_filesz_memsz_diff(elf_prog, phdr,
						     vastart, vaend);
		if (unlikely(rc)) {
			uk_pr_err("Failed to map difference between filesz and "
				  "memsz\n");
			return rc;
		}
	}

	return 0;
}

static int elf_load_fdphdr(struct elf_prog *elf_prog, GElf_Phdr *phdr, int fd)
{
	if (elf_prog->vabase && elf_prog->start)
		return do_elf_load_fdphdr_not0(elf_prog, phdr, fd);

	return do_elf_load_fdphdr_0(elf_prog, phdr, fd);
}
#else /* !CONFIG_LIBPOSIX_MMAP */
/* Read from fd exact `len` bytes from offset `roff`, fail otherwise */
static int elf_load_fdphdr_read(int fd, off_t roff, void *dst, size_t len)
{
	ssize_t rc;
	char *ptr;

	ptr = (char *)dst;
	while (len) {
		rc = pread(fd, ptr, len, roff);
		if (unlikely(rc < 0)) {
			if (errno == EINTR)
				continue; /* retry */
			/* abort on any other error */
			return -errno;
		}
		if (unlikely(rc == 0))
			break; /* end-of-file */

		/* prepare for next piece to read */
		len -= rc;
		ptr += rc;
		roff += rc;
	}

	if (unlikely(len != 0))
		return -ENOEXEC; /* unexpected EOF */
	return 0;
}

static int elf_load_fdphdr(struct elf_prog *elf_prog, GElf_Phdr *phdr, int fd)
{
	uintptr_t vastart, vaend;
	int ret;

	vastart = phdr->p_paddr + (uintptr_t)elf_prog->vabase;
	vaend   = vastart + phdr->p_filesz;
	if (!elf_prog->start || (vastart < elf_prog->start))
		elf_prog->start = vastart;

	uk_pr_debug("%s: Reading 0x%"PRIx64" - 0x%"PRIx64" to 0x%"PRIx64" - 0x%"PRIx64"\n",
		    elf_prog->name,
		    (uint64_t)phdr->p_offset,
		    (uint64_t)phdr->p_offset + phdr->p_filesz,
		    (uint64_t)vastart,
		    (uint64_t)vaend);

	ret = elf_load_fdphdr_read(fd, phdr->p_offset, (void *)vastart,
				   phdr->p_filesz);
	if (unlikely(ret < 0)) {
		uk_pr_err("%s: Read error: %s\n", elf_prog->name,
			  strerror(-ret));
		return ret;
	}

	/* Compute the area that needs to be zeroed */
	vastart = vaend;
	vaend = vastart + (phdr->p_memsz - phdr->p_filesz);
	vaend = PAGE_ALIGN_UP(vaend);
	uk_pr_debug("%s: Zeroing 0x%"PRIx64" - 0x%"PRIx64"\n",
		    elf_prog->name,
		    (uint64_t)(vastart),
		    (uint64_t)(vaend));
	memset((void *)(vastart), 0, vaend - vastart);

	return 0;
}
#endif /* !CONFIG_LIBPOSIX_MMAP */

static int elf_load_fd(struct elf_prog *elf_prog, Elf *elf, int fd)
{
	size_t phnum, phi;
	GElf_Ehdr ehdr;
	GElf_Phdr phdr;
	int ret = -1;

	UK_ASSERT(elf_prog->align && PAGE_ALIGNED(elf_prog->align));

	if (unlikely(gelf_getehdr(elf, &ehdr) == NULL)) {
		elferr_err("%s: Failed to get executable header",
			   elf_prog->name);
		ret = -ENOEXEC;
		goto err_out;
	}

#if CONFIG_LIBPOSIX_MMAP
	/* If we use mmap, let `elf_load_fdphdr` decide the vabase depending
	 * on what mmap returns. For now we simply set it to what the ELF
	 * header tells us. Ultimately, `elf_load_fdphdr` will update it for
	 * us with the new address.
	 */
	elf_prog->entry = ehdr.e_entry;
#else /* !CONFIG_LIBPOSIX_MMAP */
	elf_prog->vabase = uk_memalign(elf_prog->a, elf_prog->align,
				       elf_prog->valen);
	if (unlikely(!elf_prog->vabase)) {
		uk_pr_debug("%s: Not enough memory to load image (failed to allocate %"PRIu64" bytes)\n",
			    elf_prog->name, (uint64_t)elf_prog->valen);
		ret = -ENOMEM;
		goto err_out;
	}

	uk_pr_debug("%s: Program/Library memory region: 0x%"PRIx64"-0x%"PRIx64"\n",
		    elf_prog->name,
		    (uint64_t)elf_prog->vabase,
		    (uint64_t)elf_prog->vabase + elf_prog->valen);

	/* Load segments to allocated memory and set start & entry.
	 * Unlike in the mmap case, here we already know the vabase, so update
	 * it now.
	 */
	elf_prog->entry = (uintptr_t)elf_prog->vabase + ehdr.e_entry;
#endif /* !CONFIG_LIBPOSIX_MMAP */

	if (unlikely(elf_getphnum(elf, &phnum) == 0)) {
		elferr_err("%s: Failed to get number of program headers",
			   elf_prog->name);
		ret = -ENOEXEC;
		goto err_free_img;
	}

	/* Load path to program interpreter (typically: dynamic linker) */
	if (elf_prog->interp.required) {
		for (phi = 0; phi < phnum; ++phi) {
			if (gelf_getphdr(elf, phi, &phdr) != &phdr) {
				elferr_warn("%s: Failed to get program header %"PRIu64"\n",
					    elf_prog->name, (uint64_t) phi);
				continue;
			}
			if (phdr.p_type != PT_INTERP)
				continue;

			UK_ASSERT(!elf_prog->interp.path);

			elf_prog->interp.path = malloc(phdr.p_filesz);
			if (!elf_prog->interp.path) {
				uk_pr_err("%s: Failed to load INTERP path: %s\n",
					  elf_prog->name, strerror(-ret));
				goto err_out;
			}

			memcpy(elf_prog->interp.path,
			       &elf->e_rawfile[phdr.p_offset], phdr.p_filesz);

			/* Enforce zero termination, this should normally
			 * be the case with the PT_INTERP section content.
			 * We are playing safe here.
			 */
			elf_prog->interp.path[phdr.p_filesz - 1] = '\0';
			break;
		}
	}

	for (phi = 0; phi < phnum; ++phi) {
		if (gelf_getphdr(elf, phi, &phdr) != &phdr) {
			elferr_warn("%s: Failed to get program header %"PRIu64"\n",
				    elf_prog->name, (uint64_t) phi);
			continue;
		}
		if (phdr.p_type != PT_LOAD)
			continue;

		ret = elf_load_fdphdr(elf_prog, &phdr, fd);
		if (unlikely(ret))
			return ret;
	}

	return 0;

err_free_img:
	elf_unload_vaimg(elf_prog);
	free(elf_prog->interp.path);
	elf_prog->interp.path = NULL;
err_out:
	return ret;
}
#endif /* CONFIG_LIBVFSCORE */

#if CONFIG_LIBUKVMEM
static int elf_load_ptprotect(struct elf_prog *elf_prog, Elf *elf)
{
	uintptr_t vastart;
	uintptr_t vaend;
	uintptr_t valen;
	struct uk_vas *vas;
	GElf_Ehdr ehdr;
	GElf_Phdr phdr;
	size_t phnum, phi;
	int ret;

	vas = uk_vas_get_active();
	if (unlikely(PTRISERR(vas))) {
		uk_pr_warn("%s: Unable to set page protections bits. Continuing without. Program execution might be unsafe or fail.\n",
			   elf_prog->name);
		return 0;
	}

	if (unlikely(gelf_getehdr(elf, &ehdr) == NULL)) {
		elferr_err("%s: Failed to get executable header",
			   elf_prog->name);
		return -ENOEXEC;
	}

	if (unlikely(elf_getphnum(elf, &phnum) == 0)) {
		elferr_err("%s: Failed to get number of program headers",
			   elf_prog->name);
		return -ENOEXEC;
	}

	/*
	 * Setup memory protection
	 */
	for (phi = 0; phi < phnum; ++phi) {
		if (gelf_getphdr(elf, phi, &phdr) != &phdr) {
			elferr_warn("%s: Failed to get program header %"PRIu64"\n",
				    elf_prog->name, (uint64_t) phi);
			continue;
		}
		if (phdr.p_type != PT_LOAD)
			continue;

		vastart = phdr.p_paddr + (uintptr_t) elf_prog->vabase;
		vaend   = vastart + phdr.p_memsz;
		vastart = PAGE_ALIGN_DOWN(vastart);
		vaend   = PAGE_ALIGN_UP(vaend);
		valen   = vaend - vastart;
		uk_pr_debug("%s: Protecting 0x%"PRIx64" - 0x%"PRIx64": %c%c%c\n",
				elf_prog->name,
				(uint64_t) vastart,
				(uint64_t) vaend,
				phdr.p_flags & PF_R ? 'R' : '-',
				phdr.p_flags & PF_W ? 'W' : '-',
				phdr.p_flags & PF_X ? 'X' : '-');
		ret = uk_vma_set_attr(vas, vastart, valen,
				((phdr.p_flags & PF_R) ?
				  PAGE_ATTR_PROT_READ  : 0x0) |
				((phdr.p_flags & PF_W) ?
				  PAGE_ATTR_PROT_WRITE : 0x0) |
				((phdr.p_flags & PF_X) ?
				  PAGE_ATTR_PROT_EXEC  : 0x0),
				0);
		if (ret < 0)
			uk_pr_err("%s: Failed to set protection bits: %d. Program execution may fail or might be unsafe.\n",
				  elf_prog->name, ret);
	}
	return 0;
}

static void elf_unload_ptunprotect(struct elf_prog *elf_prog)
{
	uintptr_t vastart;
	uintptr_t vaend;
	uintptr_t valen;
	struct uk_vas *vas;
	int ret;

	vas = uk_vas_get_active();
	if (PTRISERR(vas)) {
		uk_pr_warn("%s: Unable to restore page protections bits.\n",
			   elf_prog->name);
		return;
	}

	vastart = (uintptr_t) elf_prog->vabase;
	vaend   = vastart + (uintptr_t) elf_prog->valen;
	vastart = PAGE_ALIGN_DOWN(vastart);
	vaend   = PAGE_ALIGN_UP(vaend);
	valen   = vaend - vastart;
	uk_pr_debug("%s: Restore RW- protection: 0x%"PRIx64" - 0x%"PRIx64"\n",
		    elf_prog->name, (uint64_t) vastart, (uint64_t) vaend);
	ret = uk_vma_set_attr(vas, vastart, valen,
			      (PAGE_ATTR_PROT_READ | PAGE_ATTR_PROT_WRITE), 0);
	if (unlikely(ret < 0))
		uk_pr_err("%s: Failed to restore protection bits: %d.\n",
			  elf_prog->name, ret);
}
#else /* !CONFIG_LIBUKVMEM */
#define elf_load_ptprotect(p, e) ({ 0; })
#define elf_unload_ptunprotect(p) do {} while (0)
#endif /* !CONFIG_LIBUKVMEM */

void elf_unload(struct elf_prog *elf_prog)
{
	if (elf_prog->interp.prog && !PTRISERR(elf_prog->interp.prog))
		elf_unload(elf_prog->interp.prog);
	if (elf_prog->interp.path)
		free(elf_prog->interp.path);
	elf_unload_ptunprotect(elf_prog);
	elf_unload_vaimg(elf_prog);
	uk_free(elf_prog->a, elf_prog);
}

struct elf_prog *elf_load_img(struct uk_alloc *a, void *img_base,
			      size_t img_len, const char *progname)
{
	struct elf_prog *elf_prog = NULL;
	Elf *elf;
	int ret;

	elf = elf_memory(img_base, img_len);
	if (unlikely(!elf)) {
		elferr_err("%s: Failed to initialize ELF parser\n",
			   progname);
		ret = -EBUSY;
		goto err_out;
	}

	elf_prog = uk_calloc(a, 1, sizeof(*elf_prog));
	if (unlikely(!elf_prog)) {
		ret = -ENOMEM;
		goto err_end_elf;
	}
	elf_prog->a = a;
	elf_prog->name = progname;

	ret = elf_load_parse(elf_prog, elf);
	if (unlikely(ret < 0)) {
		uk_pr_err("%s: Parsing of ELF image failed: %s (%d)\n",
			  progname, strerror(-ret), ret);
		goto err_free_elf_prog;
	}
	if (unlikely(elf_prog->interp.required)) {
		uk_pr_err("%s: Requests program interpreter: Unsupported for in-memory ELF images\n",
			  progname);
		ret = -ENOTSUP;
		goto err_free_elf_prog;
	}

	ret = elf_load_imgcpy(elf_prog, elf, img_base, img_len);
	if (unlikely(ret < 0)) {
		uk_pr_err("%s: Failed to copy the executable: %d\n",
			  progname, ret);
		goto err_free_elf_prog;
	}

	ret = elf_load_ptprotect(elf_prog, elf);
	if (unlikely(ret < 0)) {
		uk_pr_err("%s: Failed to set page protection bits: %d\n",
			  progname, ret);
		goto err_unload_vaimg;
	}

	elf_end(elf);
	return elf_prog;

err_unload_vaimg:
	elf_unload_vaimg(elf_prog);
err_free_elf_prog:
	uk_free(a, elf_prog);
err_end_elf:
	elf_end(elf);
err_out:
	return ERR2PTR(ret);
}

#if CONFIG_LIBVFSCORE
static struct elf_prog *do_elf_load_vfs(struct uk_alloc *a, const char *path,
					const char *progname, bool nointerp)
{
	int fd = -1;
#if CONFIG_APPELFLOADER_VFSEXEC_EXECBIT
	struct stat fd_stat;
#endif /* CONFIG_APPELFLOADER_VFSEXEC_EXECBIT */
	struct elf_prog *elf_prog = NULL;
	Elf *elf;
	int ret;

	fd = open(path, O_RDONLY);
	if (unlikely(fd < 0)) {
		uk_pr_err("%s: Failed to execute %s: %s\n",
			  progname, path, strerror(errno));
		ret = -errno;
		goto err_out;
	}

#if CONFIG_APPELFLOADER_VFSEXEC_EXECBIT
	/* Check for executable bit */
	ret = fstat(fd, &fd_stat);
	if (unlikely(ret != 0)) {
		uk_pr_err("%s: Failed to execute %s: %s\n",
			  progname, path, strerror(errno));
		ret = -errno;
		goto err_close_fd;
	}
	if (unlikely(!(fd_stat.st_mode & S_IXUSR))) {
		uk_pr_err("%s: Failed to execute %s: %s\n",
			  progname, path, strerror(EPERM));
		ret = -EPERM;
		goto err_close_fd;
	}
#else /* !CONFIG_APPELFLOADER_VFSEXEC_EXECBIT */
	uk_pr_debug("%s: Note, ignoring executable bit state\n", progname);
#endif /* !CONFIG_APPELFLOADER_VFSEXEC_EXECBIT */

	elf = elf_open(fd);
	if (unlikely(!elf)) {
		elferr_err("%s: Failed to initialize ELF parser\n",
			   progname);
		ret = -EBUSY;
		goto err_close_fd;
	}

	elf_prog = uk_calloc(a, 1, sizeof(*elf_prog));
	if (unlikely(!elf_prog)) {
		ret = -ENOMEM;
		goto err_end_elf;
	}
	elf_prog->a = a;
	elf_prog->name = progname;
	elf_prog->path = path;

	ret = elf_load_parse(elf_prog, elf);
	if (unlikely(ret < 0)) {
		uk_pr_err("%s: Parsing of ELF image failed: %s (%d)\n",
			  progname, strerror(-ret), ret);
		goto err_free_elf_prog;
	}
	if (unlikely(nointerp && elf_prog->interp.required)) {
		uk_pr_err("%s: Requests program interpreter: Unsupported\n",
			  progname);
		ret = -ENOTSUP;
		goto err_free_elf_prog;
	}

	ret = elf_load_fd(elf_prog, elf, fd);
	if (unlikely(ret < 0)) {
		uk_pr_err("%s: Failed to copy the executable: %d\n",
			  progname, ret);
		goto err_free_elf_prog;
	}

	ret = elf_load_ptprotect(elf_prog, elf);
	if (unlikely(ret < 0)) {
		uk_pr_err("%s: Failed to set page protection bits: %d\n",
			  progname, ret);
		goto err_unload_vaimg;
	}

	elf_end(elf);
	close(fd);
	return elf_prog;

err_unload_vaimg:
	elf_unload_vaimg(elf_prog);
err_free_elf_prog:
	uk_free(a, elf_prog);
err_end_elf:
	elf_end(elf);
err_close_fd:
	close(fd);
err_out:
	return ERR2PTR(ret);
}

struct elf_prog *elf_load_vfs(struct uk_alloc *a, const char *path,
			      const char *progname)
{
	struct elf_prog *elf_prog;
	int err;

	elf_prog = do_elf_load_vfs(a, path, progname, false);
	if (PTRISERR(elf_prog) || !elf_prog) {
		err = PTR2ERR(elf_prog);
		goto err_out;
	}

	/* Load program interpreter/dynamic loader */
	if (elf_prog->interp.required) {
		uk_pr_debug("%s: Loading program interpreter %s...\n",
			    elf_prog->name, elf_prog->interp.path);
		elf_prog->interp.prog = do_elf_load_vfs(a,
							elf_prog->interp.path,
							"<interp>", true);
		if (unlikely(PTRISERR(elf_prog->interp.prog) ||
			     !elf_prog->interp.prog)) {
			err = PTR2ERR(elf_prog->interp.prog);
			uk_pr_err("%s: Failed to load program interpreter %s: %s\n",
				  elf_prog->name, elf_prog->interp.path,
				  strerror(-err));
			goto err_unload_prog;
		}
	}

	return elf_prog;

err_unload_prog:
	elf_unload(elf_prog);
err_out:
	return ERR2PTR(err);
}
#endif /* CONFIG_LIBVFSCORE */
