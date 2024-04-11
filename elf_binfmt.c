/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2024, Unikraft GmbH and The Unikraft Authors.
 * Licensed under the BSD-3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 */

#include <uk/assert.h>
#include <uk/arch/ctx.h>
#include <uk/binfmt.h>
#include <uk/essentials.h>
#include <uk/errptr.h>
#include <uk/init.h>

#if CONFIG_LIBUKRANDOM
#include <uk/random.h>
#endif /* CONFIG_LIBUKRANDOM */

#include "elf_prog.h"

static int uk_binfmt_load_elf(struct uk_binfmt_loader_args *args)
{
	struct elf_prog *prog;
	__u64 rand[2];
	int rc;

	UK_ASSERT(args);
	UK_ASSERT(args->alloc);

	/* TODO Make elf_load_vfs() modular so that we can check the file
	 * type before we do the actual load. That will also allow us to
	 * check the parameters before we load the file, as atm we're forced
	 * to do an elf_unload() on bad parameters.
	 */
	prog = elf_load_vfs(args->alloc, args->pathname, args->progname);
	if (unlikely(PTRISERR(prog))) {
		rc = PTR2ERR(prog);
		if (rc == -ENOEXEC) {
			uk_pr_warn("%s not handled by ELF binfmt loader\n",
				   args->pathname);
			return UK_BINFMT_NOT_HANDLED;
		}
		uk_pr_err("Could not load ELF (%d)\n", rc);
		return rc;
	}

	/* Save to private data in case we are requested to unload */
	args->user = (void *)prog;

	uk_pr_debug("%s: ELF loaded to 0x%lx-0x%lx (%lx B)\n", args->progname,
		    (__u64)prog->vabase, (__u64)prog->vabase + prog->valen,
		    prog->valen);
	uk_pr_debug("%s: Entry at %p\n", args->progname, (void *)prog->entry);

#if CONFIG_LIBUKRANDOM
	uk_random_fill_buffer(rand, sizeof(rand));
#else /* !CONFIG_LIBUKRANDOM */
	/* Without random numbers, use a hardcoded seed */
	uk_pr_warn("%s: Using hard-coded random seed\n", args->progname);
	rand[0] = 0xB0B0;
	rand[1] = 0xF00D;
#endif /* !CONFIG_LIBUKRANDOM */

	rc = elf_arg_env_count(&args->argc, args->argv,
			       &args->envc, args->envp,
			       args->stack_size);
	if (unlikely(rc < 0)) {
		uk_pr_err("Could not load ELF (%d)\n", rc);
		elf_unload(prog);
		return rc;
	}

	elf_ctx_init(&args->ctx, prog, args->argv[0],
		     args->argc - 1, &args->argv[1],
		     args->envc, args->envp, rand);

	return UK_BINFMT_HANDLED;
}

static int uk_binfmt_unload_elf(struct uk_binfmt_loader_args *args)
{
	UK_ASSERT(args);
	UK_ASSERT(args->user);

	elf_unload((struct elf_prog *)args->user);

	return UK_BINFMT_HANDLED;
}

static struct uk_binfmt_loader elf_loader = {
	.name = "ELF loader",
	.type = UK_BINFMT_LOADER_TYPE_EXEC,
	.ops = {
		.load = uk_binfmt_load_elf,
		.unload = uk_binfmt_unload_elf,
	},
};

static int uk_binfmt_elf_loader_init(struct uk_init_ctx *init_ctx __unused)
{
	return uk_binfmt_register(&elf_loader);
}

uk_late_initcall(uk_binfmt_elf_loader_init, 0);
