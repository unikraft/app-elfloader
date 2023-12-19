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

static inline const char *uk_netdev_ip4addr_get(struct uk_netdev *nd)
{
	static char ip4buf[16]; /* fits "255.255.255.255\0" */
	const char *einfo;
	const char *maskbits;
	size_t ip4len;

	einfo = uk_netdev_einfo_get(nd, UK_NETDEV_IPV4_CIDR);
	if (!einfo || einfo[0] == '\0') {
		uk_pr_debug("netdev%u: No CIDR address, retry with legacy address\n",
			    uk_netdev_id_get(nd));
		goto legacy; /* try with legacy address format again */
	}
	maskbits = strchr(einfo, '/');
	if (!maskbits) {
		uk_pr_debug("netdev%u: Failed to find maskbits separator of CIDR address, retry with legacy address\n",
			    uk_netdev_id_get(nd));
		goto legacy;
	}
	ip4len = (size_t)((uintptr_t)maskbits - (uintptr_t)einfo);
	if (ip4len > 16 || ip4len < 7) {
		uk_pr_debug("netdev%u: Failed to parse IP addressof CIDR address: Length out of range\n",
			    uk_netdev_id_get(nd));
		goto legacy;
	}
	strncpy(ip4buf, einfo, ip4len);
	ip4buf[ip4len] = '\0';
	return ip4buf;

legacy:
	einfo = uk_netdev_einfo_get(nd, UK_NETDEV_IPV4_ADDR);
	if (!einfo || einfo[0] == '\0') {
		uk_pr_debug("netdev%u: No IPv4 address found\n",
			    uk_netdev_id_get(nd));
		return NULL; /* no address */
	}
	return einfo;
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

static int gen_etc_hosts(const char *fpath, mode_t fmode)
{
	int rc = 0;
#if CONFIG_APPELFLOADER_AUTOGEN_ETCHOSTS
	struct uk_netdev *nd;
	const char *ip4;
	const char *domain;
	const char *hostname;
	unsigned int i;
	int fd;

	fd = cf_create(fpath, fmode);
#if CONFIG_APPELFLOADER_AUTOGEN_SKIPEXIST
	if (fd == -EEXIST)
		return 0;
#endif /* CONFIG_APPELFLOADER_AUTOGEN_SKIPEXIST */
	if (unlikely(fd < 0))
		return fd;

#if CONFIG_APPELFLOADER_AUTOGEN_ETCHOSTS_LOCALHOST4
	/* entry for localhost */
	rc = cf_strcpy(fd, "127.0.0.1\tlocalhost\n");
	if (unlikely(rc < 0))
		goto out;
#endif /* CONFIG_APPELFLOADER_AUTOGEN_ETCHOSTS_LOCALHOST4 */

	/* hosts from interfaces */
	for (i = 0; i < uk_netdev_count(); i++) {
		nd = uk_netdev_get(i);
		if (!nd)
			continue;
		ip4 = uk_netdev_ip4addr_get(nd);
		if (!ip4)
			continue; /* no IP address, skip interface */
		hostname = uk_netdev_einfo_get(nd, UK_NETDEV_IPV4_HOSTNAME);
		if (!hostname || hostname[0] == '\0')
			continue; /* no hostname, skip interface */
		domain = uk_netdev_einfo_get(nd, UK_NETDEV_IPV4_DOMAIN);
		if (!domain || domain[0] == '\0') {
			rc = cf_nprintf(fd, 128, "%s\t%s\n", ip4, hostname);
			if (unlikely(rc < 0))
				goto out;
		} else {
			rc = cf_nprintf(fd, 128, "%s\t%s %s.%s\n",
					ip4, hostname, hostname, domain);
			if (unlikely(rc < 0))
				goto out;
		}
	}

	rc = 0;

out:
	cf_close(fd);
#endif /* CONFIG_APPELFLOADER_AUTOGEN_ETCHOSTS */
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

	rc = gen_etc_hosts("/etc/hosts", 0644);
	if (unlikely((rc < 0)))
		goto out;

	rc = 0;
out:
	return rc;
}

uk_sys_initcall(gen_etc, 0x0);
