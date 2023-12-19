/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2023, Unikraft GmbH and The Unikraft Authors.
 * Licensed under the BSD-3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 */
#ifndef APPELFLOADER_CONFFILE_H
#define APPELFLOADER_CONFFILE_H

#include <uk/config.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <uk/essentials.h>

/*
 * Helpers for generating configuration files in the filesystem
 */

/**
 * Creates a directory
 * The function checks if the path already exists and creates only the
 * directory if it does not exist.
 *
 * @param dpath Path to directory to create
 * @param dmode The directory permission mode in case the folder is created
 * @return 0 if directory was created, 1 if directory already existed,
 *         a negative errno value in case of errors
 */
int cf_mkdir(const char *dpath, mode_t dmode);

/**
 * Creates/overwrites a configuration file
 * The behavior of this function in respect of already existing files is
 * configured by the configuration options:
 * `CONFIG_APPELFLOADER_CONFFILE_SKIPEXIST`,
 * `CONFIG_APPELFLOADER_CONFFILE_ERROREXIST`,
 * and `APPELFLOADER_CONFFILE_REPLACEEXIST`
 *
 * @param dpath Path to file to create/overwrite
 * @param dmode The file permission mode in case the file is created
 * @return file descriptor on success in write-only mode,
 *         -EEXIST if file already exists and hfs.c is compiled with
 *                 `CONFIG_APPELFLOADER_CONFFILE_SKIPEXIST` or
 *                 `CONFIG_APPELFLOADER_CONFFILE_ERROREXIST`,
 *         -ENOENT if something already exists under the path but is not a
 *                 regular file,
 *         a negative errno value in case of any other error
 */
int cf_create(const char *fpath, mode_t fmode);

/*
 * The following functions return 0 if the corresponding write operation
 * completed. The functions handle I/O splitting and I/O interruptions (-EINTR).
 */

/**
 * Append a memory buffer to a file
 *
 * @param fd File descriptor
 * @param buf Reference to memory buffer
 * @param buflen Number of bytes to write from buffer
 * @return 0 on success, a negative errno code in case of errors
 */
int cf_write(int fd, const char *buf, size_t buflen);

/**
 * Append a formatted string to a file
 *
 * @param fd File descriptor
 * @param maxlen Maximum size for a format buffer
 * @param fmt Format string (see `printf()`)
 * @param ... Additional arguments depending on the format string
 * @return 0 on success, a negative errno code in case of errors
 */
int cf_nprintf(int fd, size_t maxlen, const char *fmt, ...) __printf(3, 4);

/**
 * Append a formatted string to a file
 *
 * @param fd File descriptor
 * @param maxlen Maximum size for a format buffer
 * @param fmt Format string (see `printf()`)
 * @param ap Additional arguments depending on the format string
 * @return 0 on success, a negative errno code in case of errors
 */
int cf_vnprintf(int fd, size_t maxlen, const char *fmt, va_list ap);

/**
 * Append a '\0'-terminated C-string to a file
 *
 * @param fd File descriptor
 * @param strbuf Reference to '\0'-terminated string
 * @return 0 on success, a negative errno code in case of errors
 */
int cf_strcpy(int fd, const char *strbuf);

/**
 * Append a '\0'-terminated C-string to a file
 *
 * @param fd File descriptor
 * @param strbuf Reference to '\0'-terminated string
 * @param maxlen Maximum number of bytes to append
 * @return 0 on success, a negative errno code in case of errors
 */
int cf_strncpy(int fd, const char *strbuf, size_t maxlen);

/**
 * Close a file descriptor opened with `cf_create()`
 * @param fd File descriptor to close
 */
void cf_close(int fd);

#endif /* APPELFLOADER_CONFFILE_H */
