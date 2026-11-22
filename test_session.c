/* SPDX-License-Identifier: BSD-3-Clause */
#include <uk/config.h>

#if CONFIG_APPELFLOADER_PROCESS_SESSION_TEST
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <uk/alloc.h>
#include <uk/essentials.h>
#include <uk/falloc.h>
#include <uk/paging.h>
#include <uk/plat/spinlock.h>
#include <uk/vmem.h>

#include "elf_session.h"

struct elf_session_inventory {
	unsigned long free;
	unsigned long available;
	unsigned long heap_pages;
	unsigned long tables;
	unsigned long other_pages;
	unsigned long vbytes;
	unsigned long accounted;
	unsigned int aliases;
	unsigned int processes;
	unsigned int descriptors;
	unsigned int vmas;
	int coherent;
	int ids_status;
};

static int elf_session_identifiers(struct elf_session_inventory *out)
{
	pid_t observer = getpid();

	out->processes = 0;
	out->descriptors = 0;
	for (int pid = 1; pid <= CONFIG_LIBPOSIX_PROCESS_MAX_PID; pid++) {
		if (!kill(pid, 0)) {
			out->processes++;
			if (pid != 1 && pid != observer)
				return -EBUSY;
		} else if (errno != ESRCH) {
			return -errno;
		}
	}
	for (int fd = 0; fd < CONFIG_LIBPOSIX_FDTAB_MAXFDS; fd++) {
		if (fcntl(fd, F_GETFD) >= 0)
			out->descriptors++;
		else if (errno != EBADF)
			return -errno;
	}
	return out->processes == 2 ? 0 : -EBUSY;
}

static int elf_session_fast(struct elf_session_inventory *out)
{
	struct uk_vas *kernel = uk_vas_get_kernel();
	__ssz available = uk_alloc_availmem(uk_alloc_get_default());

	if (available < 0)
		return (int)available;
	out->available = (unsigned long)available;
	out->free = UK_READ_ONCE(kernel->pt->fa->free_memory);
	out->aliases = UK_READ_ONCE(kernel->pt->shared_users);
	return 0;
}

static int elf_session_fast_equal(const struct elf_session_inventory *a,
				  const struct elf_session_inventory *b)
{
	return a->free == b->free && a->available == b->available &&
		a->aliases == b->aliases;
}

static void elf_session_paging(struct uk_vas *kernel, unsigned long *mapped,
			       unsigned long *tables)
{
	*mapped = 0;
	*tables = 0;
	for (unsigned int level = 0; level < UK_PAL_PT_LEVELS; level++) {
		*mapped += UK_READ_ONCE(kernel->pt->nr_lx_pages[level]) *
			(UK_PAGING_PAGE_Lx_SIZE(level) >> UK_PAGING_PAGE_SHIFT);
		*tables += UK_READ_ONCE(kernel->pt->nr_pt_pages[level]);
	}
}

static int elf_session_memory(struct elf_session_inventory *out)
{
	struct uk_vas *kernel = uk_vas_get_kernel();
	struct elf_session_inventory before = { 0 }, after = { 0 };
	unsigned long start = 0, end = 0, irqf;
	unsigned long mapped, mapped_after, tables_after;
	struct uk_vma *vma;
	int rc;

	out->coherent = 0;
	rc = elf_session_fast(&before);
	if (rc)
		return rc;
	elf_session_paging(kernel, &mapped, &out->tables);
	out->vmas = 0;
	out->vbytes = 0;
	out->heap_pages = 0;
	ukplat_spin_lock_irqsave(&kernel->lock, irqf);
	uk_list_for_each_entry(vma, &kernel->vma_list, vma_list) {
		out->vmas++;
		out->vbytes += vma->end - vma->start;
		if (vma->name && !strcmp(vma->name, "heap")) {
			start = vma->start;
			end = vma->end;
		}
	}
	ukplat_spin_unlock_irqrestore(&kernel->lock, irqf);
	if (!end)
		return -ENOENT;
	while (start < end) {
		unsigned int level = UK_PAL_PAGE_LEVEL;
		unsigned long span, size;
		__pte_t pte;

		rc = uk_paging_pt_walk(kernel->pt, start, &level, NULL, &pte);
		if (rc)
			return rc;
		size = UK_PAGING_PAGE_Lx_SIZE(level);
		span = size - (start & (size - 1));
		if (span > end - start)
			span = end - start;
		if (UK_PAGING_PT_Lx_PTE_PRESENT(pte, level))
			out->heap_pages += span >> UK_PAGING_PAGE_SHIFT;
		start += span;
	}
	elf_session_paging(kernel, &mapped_after, &tables_after);
	rc = elf_session_fast(&after);
	if (rc)
		return rc;
	out->free = after.free;
	out->available = after.available;
	out->aliases = after.aliases;
	if (!elf_session_fast_equal(&before, &after) ||
	    mapped != mapped_after || out->tables != tables_after)
		return -EAGAIN;
	if (mapped < out->heap_pages)
		return -EINVAL;
	out->other_pages = mapped - out->heap_pages;
	/*
	 * Freeing a heap allocation returns allocator bytes but retains its
	 * lazily committed physical backing. Account that reusable reserve
	 * and the kernel page tables without accepting lost allocator bytes,
	 * additional non-heap mappings, or additional VMA reservations.
	 */
	out->accounted = out->free +
		((out->heap_pages + out->tables) << UK_PAGING_PAGE_SHIFT);
	out->coherent = 1;
	return 0;
}

static int elf_session_balanced(const struct elf_session_inventory *a,
				const struct elf_session_inventory *b)
{
	return a->coherent && !a->aliases && a->available == b->available &&
		a->accounted == b->accounted &&
		a->other_pages == b->other_pages &&
		a->vmas == b->vmas && a->vbytes == b->vbytes &&
		a->processes == b->processes &&
		a->descriptors == b->descriptors && !a->ids_status;
}

static int elf_session_retirement(struct elf_session_inventory *out,
				  const struct elf_session_inventory *expected)
{
	struct elf_session_inventory previous = { 0 };
	unsigned int stable = 0;
	int rc;

	if (uk_vas_get_active() != uk_vas_get_kernel())
		return -EXDEV;
	for (unsigned int sample = 0; sample < 5000; sample++) {
		struct timespec delay = { .tv_nsec = 1000000 };

		if (nanosleep(&delay, NULL)) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		rc = elf_session_fast(out);
		if (rc)
			return rc;
		if (!out->aliases && elf_session_fast_equal(out, &previous) &&
		    (!expected || out->available == expected->available))
			stable++;
		else
			stable = 0;
		previous = *out;
		if (stable < 100)
			continue;
		stable = 0;
		rc = elf_session_memory(out);
		if (rc == -EAGAIN)
			continue;
		if (rc)
			return rc;
		out->ids_status = elf_session_identifiers(out);
		if (out->ids_status && out->ids_status != -EBUSY)
			return out->ids_status;
		if (!out->ids_status &&
		    (!expected || elf_session_balanced(out, expected)))
			return 0;
	}
	return -ETIMEDOUT;
}

static void elf_session_report(unsigned int index,
			       const struct elf_session_inventory *out)
{
	printf("ELF_SESSION_MEMORY index=%u raw_free=%lu allocator_free=%lu ",
	       index, out->free, out->available);
	printf("heap_pages=%lu page_tables=%lu other_pages=%lu ",
	       out->heap_pages, out->tables, out->other_pages);
	printf("vmas=%u vbytes=%lu accounted=%lu coherent=%d ids=%d\n",
	       out->vmas, out->vbytes, out->accounted, out->coherent,
	       out->ids_status);
}

static void elf_session_failure(unsigned int index, const char *phase, int code,
				const struct elf_session_inventory *after)
{
	printf("ELF_SESSION_FAIL index=%u phase=%s code=%d\n",
	       index, phase, code);
	if (after) {
		printf("ELF_SESSION_RETIREMENT free=%lu aliases=%u ",
		       after->free, after->aliases);
		printf("processes=%u descriptors=%u\n",
		       after->processes, after->descriptors);
	}
}

int elf_session_observe(int (*run)(void *), void *arg)
{
	struct elf_session_inventory baseline = { 0 }, after = { 0 };
	unsigned int index;
	int rc;

	for (index = 0; index < ELF_SESSION_COUNT; index++) {
		const struct elf_session_inventory *expected =
			index >= ELF_SESSION_WARMUP ? &baseline : NULL;

		printf("ELF_SESSION_BEGIN index=%u count=8\n", index);
		rc = run(arg);
		if (rc) {
			elf_session_failure(index, "application", rc, NULL);
			return rc;
		}
		rc = elf_session_retirement(&after, expected);
		if (rc) {
			(void)elf_session_memory(&after);
			after.ids_status = elf_session_identifiers(&after);
			elf_session_report(index, &after);
			elf_session_failure(index, "retirement", rc, &after);
			return 1;
		}
		elf_session_report(index, &after);
		if (index < ELF_SESSION_WARMUP)
			baseline = after;
		if (!elf_session_balanced(&after, &baseline)) {
			elf_session_failure(index, "inventory", 1, &after);
			return 1;
		}
		printf("ELF_SESSION_END index=%u free=%lu aliases=%u",
		       index, after.free, after.aliases);
		printf(" processes=%u descriptors=%u\n",
		       after.processes, after.descriptors);
	}
	printf("ELF_SESSION_INVENTORY_PASS sessions=8 measured=4 ");
	printf("before=%lu after=%lu aliases=0 processes=2 ",
	       baseline.free, after.free);
	printf("descriptors=%u max_pid=%u fd_limit=%u\n", after.descriptors,
	       (unsigned int)CONFIG_LIBPOSIX_PROCESS_MAX_PID,
	       (unsigned int)CONFIG_LIBPOSIX_FDTAB_MAXFDS);
	printf("ELF_SESSION_ACCOUNTING_PASS before=%lu after=%lu ",
	       baseline.accounted, after.accounted);
	printf("allocator_before=%lu allocator_after=%lu\n",
	       baseline.available, after.available);
	return 0;
}
#endif
