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

#ifndef LIBELF_HELPER
#define LIBELF_HELPER

#include <libelf.h>
#include <uk/print.h>

/* Note: fmt has to be given without newline at the end! */
#define elferr_k(klvl, fmtn, ...)					\
	do {								\
		int elf_err = elf_errno();				\
		if (elf_err != 0) {					\
			uk_printk((klvl), fmtn ": %s (%d)\n",		\
				  ##__VA_ARGS__,			\
				  elf_errmsg(elf_err), elf_err);	\
		} else {						\
			uk_printk((klvl), fmtn "\n",			\
				  ##__VA_ARGS__);			\
		}							\
	} while(0)

#define elferr_warn(fmtn, ...) elferr_k(KLVL_WARN, fmtn, ##__VA_ARGS__)
#define elferr_err(fmtn, ...)  elferr_k(KLVL_ERR,  fmtn, ##__VA_ARGS__)
#define elferr_crit(fmtn, ...) elferr_k(KLVL_CRIT, fmtn, ##__VA_ARGS__)

#endif /* LIBELF_HELPER */
