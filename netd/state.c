/*
 * This is free and unencumbered software released into the public domain.
 * 
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this software, either in source code form or as a compiled binary, for any
 * purpose, commercial or non-commercial, and by any means.
 * 
 * In jurisdictions that recognize copyright laws, the author or authors of
 * this software dedicate any and all copyright interest in the software to the
 * public domain. We make this dedication for the benefit of the public at
 * large and to the detriment of our heirs and successors. We intend this
 * dedication to be an overt act of relinquishment in perpetuity of all present
 * and future rights to this software under copyright law.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include	<sys/queue.h>

#include	<errno.h>
#include	<string.h>
#include	<stdlib.h>

#include	"state.h"
#include	"log.h"
#include	"msgbus.h"
#include	"netlink.h"
#include	"netd.h"

struct network_head networks = SLIST_HEAD_INITIALIZER(networks);

struct interface_head interfaces = SLIST_HEAD_INITIALIZER(interfaces);

/* netlink event handlers */
static void	hdl_newlink(msg_id_t, void *);
static void	hdl_dellink(msg_id_t, void *);
static void	hdl_newaddr(msg_id_t, void *);
static void	hdl_deladdr(msg_id_t, void *);

int
state_init(void) {
	msgbus_sub(MSG_NETLINK_NEWLINK, hdl_newlink);
	msgbus_sub(MSG_NETLINK_DELLINK, hdl_dellink);
	msgbus_sub(MSG_NETLINK_NEWADDR, hdl_newaddr);
	msgbus_sub(MSG_NETLINK_DELADDR, hdl_deladdr);
	return 0;
}

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

	if (find_network(name) != NULL) {
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

void
hdl_newlink(msg_id_t msgid, void *data) {
struct netlink_newlink_data	*msg = data;
struct interface		*intf;

	(void)msgid;

	/* check for duplicate interfaces */
	SLIST_FOREACH(intf, &interfaces, if_entry) {
		if (strcmp(intf->if_name, msg->nl_ifname) == 0 ||
		    intf->if_index == msg->nl_ifindex) {
			return;
		}
	}

	if ((intf = calloc(1, sizeof(*intf))) == NULL)
		panic("hdl_newlink: out of memory");

	intf->if_index = msg->nl_ifindex;
	intf->if_name = strdup(msg->nl_ifname);

	nlog(NLOG_INFO, "%s<%d>: new interface",
	     intf->if_name, intf->if_index);

	SLIST_INSERT_HEAD(&interfaces, intf, if_entry);
}

void
hdl_dellink(msg_id_t msgid, void *data) {
struct netlink_dellink_data	*msg = data;
interface_t			*intf;

	(void)msgid;

	if ((intf = find_interface_byindex(msg->dl_ifindex)) == NULL)
		return;

	nlog(NLOG_INFO, "%s<%d>: interface destroyed",
	     intf->if_name, intf->if_index);

	SLIST_REMOVE(&interfaces, intf, interface, if_entry);
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
hdl_newaddr(msg_id_t msgid, void *data) {
struct netlink_newaddr_data	*msg = data;
interface_t			*intf;
ifaddr_t			*addr;

	(void)msgid;

	if ((intf = find_interface_byindex(msg->na_ifindex)) == NULL)
		return;

	addr = ifaddr_new(msg->na_family, msg->na_addr, msg->na_plen);
	if (addr == NULL)
		/* unsupported family, etc. */
		return;

	nlog(NLOG_INFO, "%s<%d>: address added",
	     intf->if_name, intf->if_index);

	SLIST_INSERT_HEAD(&intf->if_addrs, addr, ifa_entry);
}

void
hdl_deladdr(msg_id_t msgid, void *data) {
struct netlink_deladdr_data	*msg = data;
interface_t			*intf;
	/* TODO: implement */

	(void)msgid;

	if ((intf = find_interface_byindex(msg->da_ifindex)) == NULL)
		return;

	nlog(NLOG_INFO, "%s<%d>: address removed",
	     intf->if_name, intf->if_index);
}
