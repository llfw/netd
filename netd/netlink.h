/*
 * This source code is released into the public domain.
 */

#ifndef	NETD_NETLINK_H_INCLUDED
#define	NETD_NETLINK_H_INCLUDED

/*
 * netlink support; handles receiving network-related events from the kernel
 * and dispatching them to the appropriate place.
 */

#include	<netlink/netlink.h>

#include	"msgbus.h"

struct kq;

#define	NLSOCKET_BUFSIZE	32768

typedef struct nlsocket {
	int	 ns_fd;
	char	 ns_buf[NLSOCKET_BUFSIZE];
	char	*ns_bufp;
	size_t	 ns_bufn;
} nlsocket_t;

/* initialise the netlink subsystem */
int		 nl_setup		(struct kq *);

/* create a new rtnetlink socket */
nlsocket_t 	*nlsocket_create	(int flags);

/* send a message on a netlink socket */
int		 nlsocket_send		(nlsocket_t *, struct nlmsghdr *msg,
					 size_t msglen);

/* receive a message on a netlink socket */
struct nlmsghdr	*nlsocket_recv		(nlsocket_t *);

/* close a netlink socket */
void		 nlsocket_close		(nlsocket_t *);

/*
 * msgbus events
 */

/* interface created */
#define	MSG_NETLINK_NEWLINK	MSG_ID(MSG_C_NETLINK, 1)
struct netlink_newlink_data {
	unsigned	 nl_ifindex;
	char const	*nl_ifname;
};

/* interface destroyed */
#define	MSG_NETLINK_DELLINK	MSG_ID(MSG_C_NETLINK, 2)
struct netlink_dellink_data {
	unsigned	 dl_ifindex;
};

/* interface address created */
#define	MSG_NETLINK_NEWADDR	MSG_ID(MSG_C_NETLINK, 3)
struct netlink_newaddr_data {
	unsigned	 na_ifindex;
	int		 na_family;
	int		 na_plen;
	void		*na_addr;
};

/* interface address removed */
#define	MSG_NETLINK_DELADDR	MSG_ID(MSG_C_NETLINK, 4)
struct netlink_deladdr_data {
	unsigned	 da_ifindex;
	int		 da_family;
	int		 da_plen;
	void		*da_addr;
};

#endif	/* !NETD_NETLINK_H_INCLUDED */
