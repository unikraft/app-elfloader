/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2023, Unikraft GmbH and The Unikraft Authors.
 * Licensed under the BSD-3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 */
#include <uk/config.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <utime.h>
#include <uk/syscall.h>
#include <uk/assert.h>
#include <uk/print.h>
#include <uk/essentials.h>
#include <sys/mount.h>
#include "conffile.h"

/* Provide syscall prototypes in case they are not defined by
 * <uk/syscall.h>. This can be the case whenever lib/syscall-shim
 * is switched off which disables the syscall mapping feature.
 */
long uk_syscall_r_open(long, long, long);
long uk_syscall_r_close(long);
long uk_syscall_r_write(long, long, long);
long uk_syscall_r_chmod(long, long);
long uk_syscall_r_mkdir(long, long);
long uk_syscall_r_stat(long, long);

int cf_mkdir(const char *dpath, mode_t dmode)
{
	struct stat st;
	int rc;

	/* Check if dirname already exists */
	rc = (int) uk_syscall_r_stat((long) dpath, (long) &st);
	if (unlikely(rc == 0 && !(st.st_mode & S_IFDIR))) {
		uk_pr_err("%s: Already exists and is not a directory\n",
			  dpath);
		return -ENOTDIR;
	}

	if (rc == 0) {
		/* Folder already exists */
		return 0;
	}

	/* Does not exist, create folder */
	if (unlikely(rc != -ENOENT)) {
		uk_pr_err("%s: Unexpected error: %s (%d)\n",
			  dpath, strerror(-rc), -rc);
		return rc;
	}
	rc = (int) uk_syscall_r_mkdir((long) dpath, (long) dmode);
	if (unlikely(rc < 0)) {
		uk_pr_err("%s: Failed to create directory: %s (%d)\n",
			  dpath, strerror(-rc), -rc);
		return rc;
	}
	/* Folder was created */
	return 1;
}

int cf_create(const char *fpath, mode_t fmode)
{
	struct stat st;
	int fd;
	int rc;

	/* Check if fpath already exists */
	rc = stat(fpath, &st);
	if (rc == 0) {
#if CONFIG_APPELFLOADER_AUTOGEN_ERROREXIST || \
	CONFIG_APPELFLOADER_AUTOGEN_SKIPEXIST
#if CONFIG_APPELFLOADER_AUTOGEN_ERROREXIST
		uk_pr_err("%s: Already exists\n", fpath);
#else /* CONFIG_APPELFLOADER_AUTOGEN_SKIPEXIST */
		uk_pr_warn("%s: Already exists\n", fpath);
#endif /* CONFIG_APPELFLOADER_AUTOGEN_SKIPEXIST */
		return -EEXIST;
#else /* CONFIG_APPELFLOADER_AUTOGEN_REPLACEEXIST */
		if (!(st.st_mode & S_IFREG)) {
			uk_pr_err("%s: Already exists and is not a regular file\n",
				  fpath);
			return -ENOENT;
		}
#endif /* CONFIG_APPELFLOADER_AUTOGEN_REPLACEEXIST */
	}

	uk_pr_info("Generating %s\n", fpath);
	fd = (int) uk_syscall_r_open((long) fpath, (long) O_CREAT|O_WRONLY|O_TRUNC, 0);
	if (unlikely(fd < 0)) {
		uk_pr_err("%s: Failed to create: %s (%d)\n",
			  fpath, strerror(-fd), -fd);
		return fd;
	}

	rc = (int) uk_syscall_r_chmod((long) fpath, (long) fmode);
	if (unlikely(rc < 0)) {
		uk_pr_err("%s: Failed to chmod: %s (%d)\n",
			  fpath, strerror(-rc), -rc);
		return rc;
	}

	return fd;
}

int cf_write(int fd, const char *buf, size_t buflen)
{
	ssize_t rc;

	do {
		rc = (ssize_t) uk_syscall_r_write((long) fd, (long) buf, (long) buflen);
		if (unlikely(rc < 0)) {
			if (unlikely(rc != -EAGAIN && rc != -EINTR))
				return (int)rc;
			continue;
		}

		buflen -= rc;
		buf    += rc;
	} while (buflen > 0);

	return 0;
}

void cf_close(int fd)
{
	/* Ignore errors */
	uk_syscall_r_close((long) fd);
}

int cf_nprintf(int fd, size_t maxlen, const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = cf_vnprintf(fd, maxlen, fmt, ap);
	va_end(ap);

	return rc;
}

int cf_vnprintf(int fd, size_t maxlen, const char *fmt, va_list ap)
{
	char buf[maxlen];
	ssize_t strlen;

	strlen = vsnprintf(buf, maxlen, fmt, ap);
	if (strlen <= 0)
		return (int)strlen;

	return cf_write(fd, buf, (size_t)strlen);
}

int cf_strcpy(int fd, const char *strbuf)
{
	size_t len;

	len = strlen(strbuf);
	if (len == 0)
		return 0;

	return cf_write(fd, strbuf, len);
}

int cf_strncpy(int fd, const char *strbuf, size_t maxlen)
{
	size_t len;

	len = strnlen(strbuf, maxlen);
	if (len == 0)
		return 0;

	return cf_write(fd, strbuf, len);
}
