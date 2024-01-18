/*
 * This source code is released into the public domain.
 */

#include	<sys/types.h>
#include	<sys/socket.h>

#include	<netlink/netlink.h>
#include	<netlink/netlink_route.h>

#include	<errno.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdio.h>

#include	"netlink.h"
#include	"netd.h"
#include	"kq.h"
#include	"state.h"
#include	"log.h"

static kq_disposition_t	donlread(struct kq *, int fd, void *udata);

static void	hdl_rtm_newlink(struct nlmsghdr *);
static void	hdl_rtm_dellink(struct nlmsghdr *);
static void	hdl_rtm_newaddr(struct nlmsghdr *);
static void	hdl_rtm_deladdr(struct nlmsghdr *);

static void (*handlers[])(struct nlmsghdr *) = {
	[RTM_NEWLINK] = hdl_rtm_newlink,
	[RTM_DELLINK] = hdl_rtm_dellink,
	[RTM_NEWADDR] = hdl_rtm_newaddr,
	[RTM_DELADDR] = hdl_rtm_deladdr,
};

int
nl_setup(struct kq *kq) {
struct nlsocket	*nls;
struct nlmsghdr	*rthdr;
struct iovec	 iov;
struct msghdr	 mhdr;
size_t		 msglen;
ssize_t		 n;
int		 optval, optlen, err;
int		 nl_groups[] = {
	RTNLGRP_LINK,
	RTNLGRP_NEIGH,
	RTNLGRP_NEXTHOP,
	RTNLGRP_IPV4_IFADDR,
	RTNLGRP_IPV4_ROUTE,
	RTNLGRP_IPV6_IFADDR,
	RTNLGRP_IPV6_ROUTE,
};

	if ((nls = calloc(1, sizeof(*nls))) == NULL) {
		perror("calloc");
		return -1;
	}

	if ((nls->ns_fd = socket(AF_NETLINK, 
				 SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC,
				 NETLINK_ROUTE)) == -1) {
		perror("socket(AF_NETLINK)");
		return -1;
	}

        optval = 1;
        optlen = sizeof(optval);
	if (setsockopt(nls->ns_fd, SOL_NETLINK, NETLINK_MSG_INFO, &optval,
		       optlen) != 0) {
		perror("NETLINK_MSG_INFO");
		return -1;
	}

        for (unsigned i = 0; i < ASIZE(nl_groups); i++) {
		optval = nl_groups[i];
		optlen = sizeof(optval);

		if ((err = setsockopt(nls->ns_fd, SOL_NETLINK,
				      NETLINK_ADD_MEMBERSHIP,
				      &optval, optlen)) != 0) {
			perror("NETLINK_ADD_MEMBERSHIP");
			return -1;
		}
        }

	/*
	 * ask the kernel to report all existing interfaces.  these arrive as
	 * RTM_NEWLINK messages so we just handle them in the event loop.
	 */
	msglen = NLMSG_SPACE(sizeof(struct nlmsghdr));
	if ((rthdr = malloc(msglen)) == NULL)
		panic("out of memory");

	memset(rthdr, 0, msglen);
	rthdr->nlmsg_len = msglen;
	rthdr->nlmsg_type = RTM_GETLINK;
	rthdr->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

	iov.iov_base = rthdr;
	iov.iov_len = msglen;

	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;

	if ((n = sendmsg(nls->ns_fd, &mhdr, 0)) == -1) {
		free(rthdr);
		perror("RTM_GETLINK");
		return -1;
	}

	free(rthdr);

	if (kq_register_read(kq, nls->ns_fd, donlread, nls) == -1) {
		perror("kq_register_read");
		return -1;
	}
	
	return 0;
}

static void
hdl_rtm_newlink(struct nlmsghdr *nlmsg) {
struct ifinfomsg	*ifinfo = NLMSG_DATA(nlmsg);
char			 ifname[IFNAMSIZ + 1];

	nlog(NLOG_DEBUG, "RTM_NEWLINK");

	if (if_indextoname(ifinfo->ifi_index, ifname) == NULL) {
		nlog(NLOG_DEBUG, "RTM_NEWLINK: couldn't get name for "
		     "ifindex %d", (int) ifinfo->ifi_index);
		return;
	}

	interface_created(ifname, ifinfo->ifi_index);
}

static void
hdl_rtm_dellink(struct nlmsghdr *nlmsg) {
struct ifinfomsg	*ifinfo = NLMSG_DATA(nlmsg);
struct interface	*intf;

	if ((intf = find_interface_byindex(ifinfo->ifi_index)) == NULL)
		return;

	interface_destroyed(intf);
}

static void
hdl_rtm_newaddr(struct nlmsghdr *nlmsg) {
	(void)nlmsg;
	nlog(NLOG_DEBUG, "address created");
}

static void
hdl_rtm_deladdr(struct nlmsghdr *nlmsg) {
	(void)nlmsg;
	nlog(NLOG_DEBUG, "address destroyed");
}

static void
donlmsg(struct nlmsghdr *hdr) {
	nlog(NLOG_DEBUG, "nlhdr len   = %u", (unsigned int) hdr->nlmsg_len);
	nlog(NLOG_DEBUG, "nlhdr type  = %u", (unsigned int) hdr->nlmsg_type);
	nlog(NLOG_DEBUG, "nlhdr flags = %u", (unsigned int) hdr->nlmsg_flags);
	nlog(NLOG_DEBUG, "nlhdr seq   = %u", (unsigned int) hdr->nlmsg_seq);
	nlog(NLOG_DEBUG, "nlhdr pid   = %u", (unsigned int) hdr->nlmsg_pid);

	if (hdr->nlmsg_type < ASIZE(handlers) &&
	    handlers[hdr->nlmsg_type] != NULL)
		handlers[hdr->nlmsg_type](hdr);
}

static int
donlpkt(int fd) {
//struct nlsocket		*nls = udata;
static char		 buf[32768];
struct msghdr		 msg;
struct iovec		 iov;
ssize_t			 n;
struct nlmsghdr		 *nlhdr;
//struct ifinfomsg	 ifinfo;

	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if ((n = recvmsg(fd, &msg, 0)) == -1)
		return -1;

	for (nlhdr = (struct nlmsghdr *) buf; NLMSG_OK(nlhdr, n);
			nlhdr = NLMSG_NEXT(nlhdr, n)) {
		nlog(NLOG_DEBUG, "n = %u", (unsigned int) n);

		donlmsg(nlhdr);
	}

	return 0;
}

static kq_disposition_t
donlread(struct kq *kq, int fd, void *udata) {
	(void)kq;
	(void)udata;

	while (donlpkt(fd) == 0)
		;

	if (errno == EAGAIN)
		return KQ_REARM;
	else
		panic("donlpkt failed: %s", strerror(errno));
}
