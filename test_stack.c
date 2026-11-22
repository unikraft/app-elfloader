/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <uk/alloc.h>
#include <uk/print.h>
#include <uk/test.h>

#include "elf_prog.h"

#define TEST_STACK_BYTES (128 * 1024)
#define TEST_GUARD_BYTES 128
#define TEST_VECTOR_COUNT 4096

static bool stack_contains(uintptr_t base, uintptr_t value, __sz size)
{
	return value >= base && value < base + TEST_STACK_BYTES &&
		size <= base + TEST_STACK_BYTES - value;
}

/* Helpers return status; all test assertions remain inside their cases. */
static int stack_context_check(bool prepend, int nargs, int nenv)
{
	struct uk_alloc *a = uk_alloc_get_default();
	struct elf_prog prog = { .name = "metadata-name", .entry = 1 };
	struct ukarch_ctx ctx = { 0 };
	__u64 random[2] = { 0x1234, 0x5678 };
	const __u64 expected_random[2] = { 0x1234, 0x5678 };
	const char **args = NULL, **env = NULL, *prefix;
	char argument[] = "arg", variable[] = "A=1";
	unsigned char *storage = NULL;
	char *path = NULL;
	uintptr_t base, *words, *argv, *envp, *aux;
	int argc, envc, count, i, rc = -ENOMEM;
	bool seed_found = false, path_found = false, terminated = false;

	args = uk_malloc(a, ((__sz)nargs + 1) * sizeof(*args));
	env = uk_malloc(a, ((__sz)nenv + 1) * sizeof(*env));
	storage = uk_malloc(a, TEST_STACK_BYTES + 2 * TEST_GUARD_BYTES);
	path = uk_malloc(a, 32);
	if (!args || !env || !storage || !path)
		goto out;
	memcpy(path, "/metadata/path", sizeof("/metadata/path"));
	prog.path = path;
	for (i = 0; i < nargs; i++)
		args[i] = i == nargs - 1 ? "" : argument;
	args[nargs] = NULL;
	for (i = 0; i < nenv; i++)
		env[i] = i == nenv - 1 ? "" : variable;
	env[nenv] = NULL;
	prefix = prepend ? (nargs ? "prefix" : "") : NULL;
	rc = elf_ctx_check_args(&prog, prefix, &argc, args, &envc, env,
				TEST_STACK_BYTES);
	if (rc)
		goto out;
	memset(storage, 0xa5, TEST_STACK_BYTES + 2 * TEST_GUARD_BYTES);
	base = (uintptr_t)(storage + TEST_GUARD_BYTES);
	ctx.sp = base + TEST_STACK_BYTES;
	elf_ctx_init(&ctx, &prog, prefix, argc, args, envc, env, random);
	/* All strings and random data must survive their source changing. */
	memcpy(path, "changed", sizeof("changed"));
	argument[0] = 'x';
	variable[0] = 'x';
	memset(random, 0, sizeof(random));
	rc = -EIO;
	count = nargs + prepend;
	if (!stack_contains(base, ctx.sp, 3 * sizeof(*words)) ||
	    !IS_ALIGNED(ctx.sp, UKARCH_SP_ALIGN))
		goto out;
	words = (uintptr_t *)ctx.sp;
	/* ukarch_ctx_init() pushed a call trampoline and the image entry. */
	if (words[1] != prog.entry)
		goto out;
	words += 2;
	if (words[0] != (uintptr_t)count)
		goto out;
	argv = words + 1;
	envp = argv + count + 1;
	aux = envp + nenv + 1;
	if (!stack_contains(base, (uintptr_t)aux, 2 * sizeof(*aux)) ||
	    argv[count] || envp[nenv])
		goto out;
	for (i = 0; i < count; i++) {
		const char *expected = prepend && !i ? prefix :
			(i - prepend == nargs - 1 ? "" : "arg");

		if (!stack_contains(base, argv[i], strlen(expected) + 1) ||
		    strcmp((const char *)argv[i], expected))
			goto out;
	}
	for (i = 0; i < nenv; i++) {
		const char *expected = i == nenv - 1 ? "" : "A=1";

		if (!stack_contains(base, envp[i], strlen(expected) + 1) ||
		    strcmp((const char *)envp[i], expected))
			goto out;
	}
	for (i = 0; i < 32; i++, aux += 2) {
		if (!stack_contains(base, (uintptr_t)aux, 2 * sizeof(*aux)))
			goto out;
		if (!aux[0]) {
			terminated = true;
			break;
		}
		if (aux[0] == 25) {
			if (!stack_contains(base, aux[1],
					    sizeof(expected_random)) ||
			    memcmp((void *)aux[1], expected_random,
				   sizeof(expected_random)))
				goto out;
			seed_found = true;
		}
		if (aux[0] == 31) {
			if (!stack_contains(base, aux[1],
					    sizeof("/metadata/path")) ||
			    strcmp((const char *)aux[1], "/metadata/path"))
				goto out;
			path_found = true;
		}
	}
	if (!seed_found || !path_found || !terminated)
		goto out;
	for (i = 0; i < TEST_GUARD_BYTES; i++)
		if (storage[i] != 0xa5 ||
		    storage[TEST_GUARD_BYTES + TEST_STACK_BYTES + i] != 0xa5)
			goto out;
	rc = 0;
out:
	uk_free(a, path);
	uk_free(a, storage);
	uk_free(a, env);
	uk_free(a, args);
	return rc;
}

UK_TESTCASE(elf_initial_stack, vectors_and_copied_auxiliary_data)
{
	int plain, prefixed, empty, large;

	plain = stack_context_check(false, 3, 2);
	prefixed = stack_context_check(true, 3, 2);
	empty = stack_context_check(true, 0, 0);
	large = stack_context_check(false, TEST_VECTOR_COUNT,
				    TEST_VECTOR_COUNT);
	UK_TEST_EXPECT_ZERO(plain);
	UK_TEST_EXPECT_ZERO(prefixed);
	UK_TEST_EXPECT_ZERO(empty);
	UK_TEST_EXPECT_ZERO(large);
	if (!plain && !prefixed && !empty && !large)
		uk_pr_info("ELF_STACK_PASS contexts=4 vectors=4096 guard_bytes=128\n");
}

UK_TESTCASE(elf_initial_stack, bounded_strings_and_pointer_budget)
{
	struct uk_alloc *a = uk_alloc_get_default();
	struct elf_prog prog = { .name = "name", .path = "/image" };
	const char *one[2] = { "", NULL }, *huge[2] = { NULL, NULL };
	const char **many;
	const __sz limit = MAX((__sz)ARG_MAX, TEST_STACK_BYTES / 4);
	char *text = uk_malloc(a, limit + 2);
	int argc, envc, i, rc;

	many = uk_malloc(a, (TEST_VECTOR_COUNT + 1) * sizeof(*many));
	UK_TEST_EXPECT_NOT_NULL(many);
	UK_TEST_EXPECT_NOT_NULL(text);
	if (!many || !text)
		goto out;
	for (i = 0; i < TEST_VECTOR_COUNT; i++)
		many[i] = "";
	many[TEST_VECTOR_COUNT] = NULL;
	rc = elf_ctx_check_args(&prog, NULL, &argc, many, &envc, NULL, 4096);
	UK_TEST_EXPECT_SNUM_EQ(rc, -E2BIG);
	rc = elf_ctx_check_args(&prog, NULL, &argc, one, &envc, many, 4096);
	UK_TEST_EXPECT_SNUM_EQ(rc, -E2BIG);
	rc = elf_ctx_check_args(&prog, NULL, &argc, one, &envc, NULL, 512);
	UK_TEST_EXPECT_SNUM_EQ(rc, -E2BIG);
	rc = elf_ctx_check_args(&prog, NULL, &argc, one, &envc, NULL, 4096);
	UK_TEST_EXPECT_ZERO(rc);
	rc = elf_ctx_check_args(&prog, NULL, &argc, NULL, &envc, NULL, 4096);
	UK_TEST_EXPECT_SNUM_EQ(rc, -EINVAL);
	rc = elf_ctx_check_args(&prog, "", &argc, NULL, &envc, NULL, 4096);
	UK_TEST_EXPECT_ZERO(rc);
	memset(text, 'x', limit + 1);
	text[limit + 1] = '\0';
	huge[0] = text;
	rc = elf_ctx_check_args(&prog, NULL, &argc, huge, &envc, NULL,
				TEST_STACK_BYTES);
	UK_TEST_EXPECT_SNUM_EQ(rc, -E2BIG);
	prog.path = text;
	rc = elf_ctx_check_args(&prog, NULL, &argc, one, &envc, NULL, 4096);
	UK_TEST_EXPECT_SNUM_EQ(rc, -E2BIG);
	uk_pr_info("ELF_STACK_PASS bounds cases=8\n");
out:
	uk_free(a, text);
	uk_free(a, many);
}

_UK_TESTSUITE(elf_initial_stack, NULL);

static void __constructor run_elf_initial_stack(void)
{
	uk_testsuite_run(&_UK_TESTSUITE_NAME(elf_initial_stack));
}
