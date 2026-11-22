/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef ELF_SESSION_H
#define ELF_SESSION_H

enum {
	ELF_SESSION_COUNT = 8,
	ELF_SESSION_WARMUP = 4
};

int elf_session_observe(int (*run)(void *), void *arg);

#endif
