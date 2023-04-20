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

#include <libelf.h>
#include <gelf.h>
#include <errno.h>
#include <string.h>
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
#if CONFIG_PAGING
#include <uk/plat/paging.h>
#include <uk/arch/limits.h>
#endif /* CONFIG_PAGING */

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
	uintptr_t prog_lowerl;
	uintptr_t prog_upperl;
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
	prog_lowerl = 0;
	prog_upperl = 0;
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
	}
	uk_pr_debug("%s: base: pie + 0x%"PRIx64", len: 0x%"PRIx64"\n",
		    elf_prog->name, prog_lowerl, prog_upperl - prog_lowerl);

	/* We do not support yet an img base other than 0 */
	if (unlikely(prog_lowerl != 0)) {
		elferr_err("%s: Image base is not 0x0, unsupported\n",
			   elf_prog->name);
		ret = -ENOEXEC;
		goto err_out;
	}

	elf_prog->phdr.off = ehdr.e_phoff;
	elf_prog->phdr.num = ehdr.e_phnum;
	elf_prog->phdr.entsize = ehdr.e_phentsize;
	elf_prog->valen = (prog_upperl - prog_lowerl);
	return 0;

err_out:
	return ret;
}

static void elf_unload_vaimg(struct elf_prog *elf_prog)
{
	if (elf_prog->vabase) {
		uk_free(elf_prog->a, elf_prog->vabase);
		elf_prog->vabase = NULL;
		elf_prog->start = 0;
		elf_prog->entry = 0;
	}
}

static int elf_load_imgcpy(struct elf_prog *elf_prog, Elf *elf,
			   const void *img_base, size_t img_len __unused)
{
	uintptr_t vastart;
	uintptr_t vaend;
	GElf_Ehdr ehdr;
	GElf_Phdr phdr;
	size_t phnum, phi;
	int ret;

	if (unlikely(gelf_getehdr(elf, &ehdr) == NULL)) {
		elferr_err("%s: Failed to get executable header",
			   elf_prog->name);
		ret = -ENOEXEC;
		goto err_out;
	}

	elf_prog->vabase = uk_memalign(elf_prog->a, __PAGE_SIZE,
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
/* Read from fd exact `len` bytes from offset `roff`, fail otherwise */
static int elf_load_do_fdread(int fd, off_t roff, void *dst, size_t len)
{
	char *ptr;
	ssize_t rc;

	ptr = (char *) dst;
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

static int elf_load_fdread(struct elf_prog *elf_prog, Elf *elf, int fd)
{
	uintptr_t vastart;
	uintptr_t vaend;
	GElf_Ehdr ehdr;
	GElf_Phdr phdr;
	size_t phnum, phi;
	int ret = -1;

	if (unlikely(gelf_getehdr(elf, &ehdr) == NULL)) {
		elferr_err("%s: Failed to get executable header",
			   elf_prog->name);
		ret = -ENOEXEC;
		goto err_out;
	}

	elf_prog->vabase = uk_memalign(elf_prog->a, __PAGE_SIZE,
				       elf_prog->valen);
	if (unlikely(!elf_prog->vabase)) {
		uk_pr_debug("%s: Not enough memory to load image (failed to allocate %"PRIu64" bytes)\n",
			    elf_prog->name, (uint64_t) elf_prog->valen);
		ret = -ENOMEM;
		goto err_out;
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

		uk_pr_debug("%s: Reading 0x%"PRIx64" - 0x%"PRIx64" to 0x%"PRIx64" - 0x%"PRIx64"\n",
			    elf_prog->name,
			    (uint64_t) phdr.p_offset,
			    (uint64_t) phdr.p_offset + phdr.p_filesz,
			    (uint64_t) vastart,
			    (uint64_t) vaend);
		ret = elf_load_do_fdread(fd, phdr.p_offset, (void *) vastart,
					 phdr.p_filesz);
		if (unlikely(ret < 0)) {
			uk_pr_err("%s: Read error: %s\n", elf_prog->name,
				  strerror(-ret));
			goto err_free_img;
		}

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
#endif /* CONFIG_LIBVFSCORE */

#if CONFIG_PAGING
static int elf_load_ptprotect(struct elf_prog *elf_prog, Elf *elf)
{
	uintptr_t vastart;
	uintptr_t vaend;
	struct uk_pagetable *pt;
	GElf_Ehdr ehdr;
	GElf_Phdr phdr;
	size_t phnum, phi;
	int ret;

	pt = ukplat_pt_get_active();
	if (unlikely(PTRISERR(pt))) {
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
		uk_pr_debug("%s: Protecting 0x%"PRIx64" - 0x%"PRIx64": %c%c%c\n",
				elf_prog->name,
				PAGE_ALIGN_DOWN((uint64_t) (vastart)),
				PAGE_ALIGN_UP((uint64_t) (vaend)),
				phdr.p_flags & PF_R ? 'R' : '-',
				phdr.p_flags & PF_W ? 'W' : '-',
				phdr.p_flags & PF_X ? 'X' : '-');
		ret = ukplat_page_set_attr(pt, PAGE_ALIGN_DOWN((__vaddr_t)
				vastart), PAGE_ALIGN_UP(phdr.p_memsz),
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
#else /* !CONFIG_PAGING */
#define elf_load_ptprotect(p, e) ({ 0; })
#endif /* !CONFIG_PAGING */

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
struct elf_prog *elf_load_vfs(struct uk_alloc *a, const char *path,
			      const char *progname)
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
	if (unlikely(elf_prog->interp.required)) {
		uk_pr_err("%s: Requests program interpreter: Unsupported\n",
			  progname);
		ret = -ENOTSUP;
		goto err_free_elf_prog;
	}

	ret = elf_load_fdread(elf_prog, elf, fd);
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
#endif /* CONFIG_LIBVFSCORE */
