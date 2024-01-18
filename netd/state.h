/*
 * This source code is released into the public domain.
 */

#ifndef	NETD_STATE_H_INCLUDED
#define	NETD_STATE_H_INCLUDED

#include	<sys/queue.h>

/*
 * manage running system state (mainly interfaces).
 */

typedef struct network {
	SLIST_ENTRY(network)	 net_entry;
	char const		*net_name;
} network_t;

extern SLIST_HEAD(network_head, network) networks;

network_t	*find_network(char const *name);
network_t	*create_network(char const *name);

typedef struct interface {
	SLIST_ENTRY(interface)	 if_entry;
	char const		*if_name;
	unsigned int		 if_index;
	struct network		*if_network;
} interface_t;

extern SLIST_HEAD(interface_head, interface) interfaces;

/* find an existing interface */
interface_t	*find_interface_byname(char const *name);
interface_t	*find_interface_byindex(unsigned int ifindex);

/*
 * inform state that a new interface was created or an existing interface
 * was destroyed.
 */
interface_t	*interface_created(char const *name,
				   unsigned int ifindex);
void		 interface_destroyed(interface_t *);

#endif	/* !NETD_STATE_H_INCLUDED */
