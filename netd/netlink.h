/*
 * This source code is released into the public domain.
 */

#ifndef	NETD_NETLINK_H_INCLUDED
#define	NETD_NETLINK_H_INCLUDED

/*
 * netlink support; handles receiving network-related events from the kernel
 * and dispatching them to the appropriate place.
 */

struct kq;

struct nlsocket {
	int	ns_fd;
};

int	nl_setup(struct kq *);

#endif	/* !NETD_NETLINK_H_INCLUDED */
