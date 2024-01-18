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

struct kq;

#define	NLSOCKET_BUFSIZE	32768

typedef struct nlsocket {
	int	 ns_fd;
	char	 ns_buf[NLSOCKET_BUFSIZE];
	char	*ns_bufp;
	size_t	 ns_bufn;
} nlsocket_t;

int		 nl_setup		(struct kq *);
nlsocket_t 	*nlsocket_create	(int flags);
int		 nlsocket_send		(nlsocket_t *, struct nlmsghdr *msg,
					 size_t msglen);
struct nlmsghdr	*nlsocket_recv		(nlsocket_t *);
void		 nlsocket_close		(nlsocket_t *);

#endif	/* !NETD_NETLINK_H_INCLUDED */
