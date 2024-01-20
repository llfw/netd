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

#ifndef	NETD_NETLINK_H_INCLUDED
#define	NETD_NETLINK_H_INCLUDED

/*
 * netlink support; handles receiving network-related events from the kernel
 * and dispatching them to the appropriate place.
 */

#include	<netlink/netlink.h>
#include	<netlink/route/interface.h>

#include	"msgbus.h"
#include	"defs.h"

#define	NLSOCKET_BUFSIZE	32768

typedef struct nlsocket {
	char		ns_buf[NLSOCKET_BUFSIZE];
	int		ns_fd;
	char * nonnull	ns_bufp;
	size_t		ns_bufn;
} nlsocket_t;

/* initialise the netlink subsystem */
int nlinit(void);

/* create a new rtnetlink socket */
nlsocket_t *nullable newmem nlsocket_create(int flags);

/* send a message on a netlink socket */
int nlsocket_send(nlsocket_t *nonnull,
		  struct nlmsghdr *nonnull msg,
		  size_t msglen);

/* receive a message on a netlink socket */
struct nlmsghdr	*nullable nlsocket_recv(nlsocket_t *nonnull);

/* close a netlink socket */
void nlsocket_close(nlsocket_t *nonnull);

/*
 * msgbus events
 */

/* interface created */
#define	MSG_NETLINK_NEWLINK	MSG_ID(MSG_C_NETLINK, 1)
struct netlink_newlink_data {
	unsigned				nl_ifindex;
	char const * nonnull			nl_ifname;
	struct rtnl_link_stats64 * nonnull	nl_stats;
};

/* interface destroyed */
#define	MSG_NETLINK_DELLINK	MSG_ID(MSG_C_NETLINK, 2)
struct netlink_dellink_data {
	unsigned	 dl_ifindex;
};

/* interface address created */
#define	MSG_NETLINK_NEWADDR	MSG_ID(MSG_C_NETLINK, 3)
struct netlink_newaddr_data {
	unsigned	na_ifindex;
	int		na_family;
	int		na_plen;
	void * nonnull	na_addr;
};

/* interface address removed */
#define	MSG_NETLINK_DELADDR	MSG_ID(MSG_C_NETLINK, 4)
struct netlink_deladdr_data {
	unsigned	da_ifindex;
	int		da_family;
	int		da_plen;
	void * nonnull	da_addr;
};

#endif	/* !NETD_NETLINK_H_INCLUDED */
