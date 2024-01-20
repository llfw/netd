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

#ifndef	NETD_STATE_H_INCLUDED
#define	NETD_STATE_H_INCLUDED

#include	<sys/types.h>
#include	<sys/socket.h>
#include	<sys/queue.h>
#include	<sys/uuid.h>

#include	<netinet/in.h>
#include	<netinet/if_ether.h>

#include	"db.h"
#include	"defs.h"

/*
 * manage running system state (mainly interfaces).
 */

int	state_init(void);

typedef struct network {
	struct network *nullable	net_next;
	char const *nonnull		net_name;
} network_t;

extern network_t *nullable networks;

network_t *nullable find_network(char const *nonnull name);
network_t *nullable create_network(char const *nonnull name);

/* an address assigned to an interface */
typedef struct ifaddr {
	struct ifaddr *nullable	ifa_next;
	int			ifa_family;
	union {
		struct ether_addr	ifa_ether;
		struct in_addr		ifa_addr4;
		struct in6_addr		ifa_addr6;
	};
	int			ifa_plen;	/* prefix length */
} ifaddr_t;

ifaddr_t *nullable ifaddr_new(int family, void *nonnull addr, int plen);

/*
 * an interface.  this represents an interface which is active on the system
 * right now.
 */

/* how often to calculate interface stats, in seconds */
#define	INTF_STATE_INTERVAL	5
/* how many previous periods to store */
#define	INTF_STATE_HISTORY	6	/* 6 * 5 = 30 seconds */

typedef struct interface {
	struct interface *nullable	if_next;
	struct uuid			if_uuid;
	char const *nonnull		if_name;
	unsigned int			if_index;
	ifaddr_t *nullable		if_addrs;
	network_t *nullable		if_network;
	pinterface_t *nullable		if_pintf;	/* persistent config */
	/* stats history */
	uint64_t			if_obytes[INTF_STATE_HISTORY];
	uint64_t			if_txrate; /* bps */
	uint64_t			if_ibytes[INTF_STATE_HISTORY];
	uint64_t			if_rxrate; /* bps */
} interface_t;

extern interface_t *nullable interfaces;

/* find an existing interface */
interface_t *nullable find_interface_byname(char const *nonnull name);
interface_t *nullable find_interface_byindex(unsigned int ifindex);

/*
 * inform state that something changed with an interface.
 */
#if 0
interface_t	*interface_created(char const *name,
				   unsigned int ifindex);
void		 interface_destroyed(interface_t *);
void		 interface_address_added(interface_t *, ifaddr_t *);
#endif

#endif	/* !NETD_STATE_H_INCLUDED */
