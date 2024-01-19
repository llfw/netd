/*
 * This source code is released into the public domain.
 */

#include	<sys/queue.h>

#include	<errno.h>
#include	<string.h>
#include	<stdlib.h>

#include	"state.h"
#include	"log.h"

struct network_head networks = SLIST_HEAD_INITIALIZER(networks);

struct interface_head interfaces = SLIST_HEAD_INITIALIZER(interfaces);

struct network *
find_network(char const *name) {
struct network	*network;

	SLIST_FOREACH(network, &networks, net_entry) {
		if (strcmp(name, network->net_name) == 0)
			return network;
	}

	errno = ESRCH;
	return NULL;
}

struct network *
create_network(char const *name) {
struct network	*net;

	if ((net = find_network(name)) != NULL) {
		errno = EEXIST;
		return NULL;
	}

	if ((net = calloc(1, sizeof(*net))) == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	net->net_name = strdup(name);
	return net;
}

struct interface *
find_interface_byname(char const *name) {
struct interface	*intf;

	SLIST_FOREACH(intf, &interfaces, if_entry) {
		if (strcmp(name, intf->if_name) == 0)
			return intf;
	}

	errno = ESRCH;
	return NULL;
}

struct interface *
find_interface_byindex(unsigned int ifindex) {
struct interface	*intf;

	SLIST_FOREACH(intf, &interfaces, if_entry) {
		if (intf->if_index == ifindex)
			return intf;
	}

	errno = ESRCH;
	return NULL;
}

struct interface *
interface_created(char const *name, unsigned int ifindex) {
struct interface	*intf;

	SLIST_FOREACH(intf, &interfaces, if_entry) {
		if (strcmp(intf->if_name, name) == 0 ||
		    intf->if_index == ifindex) {
			errno = EEXIST;
			return NULL;
		}
	}

	if ((intf = calloc(1, sizeof(*intf))) == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	intf->if_name = strdup(name);
	intf->if_index = ifindex;

	nlog(NLOG_INFO, "%s<%d>: new interface",
	     intf->if_name, intf->if_index);

	SLIST_INSERT_HEAD(&interfaces, intf, if_entry);
	return intf;
}

void
interface_destroyed(struct interface *iface) {
	SLIST_REMOVE(&interfaces, iface, interface, if_entry);
}

ifaddr_t *
ifaddr_new(int family, void *addr, int plen) {
ifaddr_t	*ret = NULL;

	if (plen < 0)
		goto err;

	if ((ret = calloc(1, sizeof(*ret))) == NULL)
		goto err;

	switch (family) {
	case AF_INET:
		if (plen > 32)
			goto err;

		ret->ifa_family = AF_INET;
		memcpy(&ret->ifa_addr4, addr, sizeof(struct in_addr));
		ret->ifa_plen = plen;
		break;

	case AF_INET6:
		if (plen > 128)
			goto err;

		ret->ifa_family = AF_INET6;
		memcpy(&ret->ifa_addr6, addr, sizeof(struct in6_addr));
		ret->ifa_plen = plen;
		break;

	case AF_LINK:
		/* Ethernet addresses don't have a mask */
		if (plen != 48)
			goto err;

		ret->ifa_family = AF_LINK;
		memcpy(&ret->ifa_ether, addr, sizeof(struct ether_addr));
		ret->ifa_plen = plen;
		break;
	}

	return ret;

err:
	if (ret)
		free(ret);

	return NULL;
}

void
interface_address_added(interface_t *intf, ifaddr_t *addr) {
	SLIST_INSERT_HEAD(&intf->if_addrs, addr, ifa_entry);
}
