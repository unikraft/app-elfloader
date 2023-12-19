/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2023, Unikraft GmbH and The Unikraft Authors.
 * Licensed under the BSD-3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 */
#include <uk/config.h>
#include <string.h>
#include "conffile.h"
#include <uk/essentials.h>
#include <uk/init.h>
#include <uk/netdev.h>

static struct uk_netdev *uk_netdev_find_einfo(int einfo_property)
{
	struct uk_netdev *nd;
	const char *einfo;
	unsigned int i;

	/* First we check if we have one device with a DNS server entry,
	 * otherwise, we do not create/overwrite a /etc/resolv.conf
	 */
	for (i = 0; i < uk_netdev_count(); i++) {
		nd = uk_netdev_get(i);
		if (!nd)
			continue;
		einfo = uk_netdev_einfo_get(nd, einfo_property);
		if (einfo && einfo[0] != '\0')
			return nd;
	}
	return NULL;
}

static int gen_etc_resolvconf(const char *fpath, mode_t fmode)
{
	int rc = 0;
#if CONFIG_APPELFLOADER_AUTOGEN_ETCRESOLVCONF
	struct uk_netdev *nd;
	const char *primary_domain;
	const char *einfo;
	unsigned int i;
	int fd;

	fd = cf_create(fpath, fmode);
#if CONFIG_APPELFLOADER_AUTOGEN_SKIPEXIST
	if (fd == -EEXIST)
		return 0;
#endif /* CONFIG_APPELFLOADER_AUTOGEN_SKIPEXIST */
	if (unlikely(fd < 0))
		return fd;

	/* nameservers */
	for (i = 0; i < uk_netdev_count(); i++) {
		nd = uk_netdev_get(i);
		if (!nd)
			continue;

		einfo = uk_netdev_einfo_get(nd, UK_NETDEV_IPV4_DNS0);
		if (einfo && einfo[0] != '\0') {
			rc = cf_nprintf(fd, 128, "nameserver %s\n", einfo);
			if (unlikely(rc < 0))
				goto out;
		}

		einfo = uk_netdev_einfo_get(nd, UK_NETDEV_IPV4_DNS1);
		if (einfo && einfo[0] != '\0') {
			rc = cf_nprintf(fd, 128, "nameserver %s\n", einfo);
			if (unlikely(rc < 0))
				goto out;
		}
	}

	/* detect primary domain (we take the first one that we can find)*/
	primary_domain = NULL;
	nd = uk_netdev_find_einfo(UK_NETDEV_IPV4_DOMAIN);
	if (nd)
		primary_domain = uk_netdev_einfo_get(nd, UK_NETDEV_IPV4_DOMAIN);

	/* search domains and host domain */
	if (primary_domain) {
		/* Because we have a primary domain, there is at least one
		 * domain that we can also use as search domain. So, it is safe
		 * to start the line with the "search" keyword already.
		 */
		rc = cf_strcpy(fd, "search");
		if (unlikely(rc < 0))
			goto out;
		for (i = 0; i < uk_netdev_count(); i++) {
			nd = uk_netdev_get(i);
			if (!nd)
				continue;

			einfo = uk_netdev_einfo_get(nd, UK_NETDEV_IPV4_DOMAIN);
			if (einfo && einfo[0] != '\0') {
				rc = cf_nprintf(fd, 128, " %s", einfo);
				if (unlikely(rc < 0))
					goto out;
			}
		}
		rc = cf_strcpy(fd, "\n");
		if (unlikely(rc < 0))
			goto out;

		/* primary domain */
		rc = cf_nprintf(fd, 128, "domain %s\n", primary_domain);
		if (unlikely(rc < 0))
			goto out;
	}
	rc = 0;

out:
	cf_close(fd);
#endif /* CONFIG_APPELFLOADER_AUTOGEN_ETCRESOLVCONF */
	return rc;
}

static int gen_etc(struct uk_init_ctx *ictx __unused)
{
	int rc;

	rc = cf_mkdir("/etc", 0755);
	if (unlikely((rc < 0)))
		goto out;

	rc = gen_etc_resolvconf("/etc/resolv.conf", 0644);
	if (unlikely((rc < 0)))
		goto out;

	rc = 0;
out:
	return rc;
}

uk_sys_initcall(gen_etc, 0x0);
