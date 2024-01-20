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

/*
 * manage running system state (mainly interfaces).
 */

int	state_init(void);

typedef struct network {
	SLIST_ENTRY(network)	 net_entry;
	char const		*net_name;
} network_t;

extern SLIST_HEAD(network_head, network) networks;

network_t	*find_network(char const *name);
network_t	*create_network(char const *name);

/* an address assigned to an interface */
typedef struct ifaddr {
	SLIST_ENTRY(ifaddr)	ifa_entry;
	int			ifa_family;
	union {
		struct ether_addr	ifa_ether;
		struct in_addr		ifa_addr4;
		struct in6_addr		ifa_addr6;
	};
	int			ifa_plen;	/* prefix length */
} ifaddr_t;

typedef SLIST_HEAD(ifaddr_list, ifaddr) ifaddr_list_t;

ifaddr_t	*ifaddr_new(int family, void *addr, int plen);

/*
 * an interface.  this represents an interface which is active on the system
 * right now.
 */

/* how often to calculate interface stats, in seconds */
#define	INTF_STATE_INTERVAL	5
/* how many previous periods to store */
#define	INTF_STATE_HISTORY	6	/* 6 * 5 = 30 seconds */

typedef struct interface {
	SLIST_ENTRY(interface)	 if_entry;
	struct uuid		 if_uuid;	/* uuid in persistent store */
	char const		*if_name;
	unsigned int		 if_index;
	ifaddr_list_t		 if_addrs;
	struct network		*if_network;	/* may be NULL */
	pinterface_t		*if_pintf;	/* persistent config */
	/* stats history */
	uint64_t		 if_obytes[INTF_STATE_HISTORY];
	uint64_t		 if_txrate; /* bps */
	uint64_t		 if_ibytes[INTF_STATE_HISTORY];
	uint64_t		 if_rxrate; /* bps */
} interface_t;

extern SLIST_HEAD(interface_head, interface) interfaces;

/* find an existing interface */
interface_t	*find_interface_byname(char const *name);
interface_t	*find_interface_byindex(unsigned int ifindex);

/*
 * inform state that something changed with an interface.
 */
interface_t	*interface_created(char const *name,
				   unsigned int ifindex);
void		 interface_destroyed(interface_t *);
void		 interface_address_added(interface_t *, ifaddr_t *);

#endif	/* !NETD_STATE_H_INCLUDED */
