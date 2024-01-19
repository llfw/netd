/*
 * This source code is released into the public domain.
 */

#include	<sys/types.h>
#include	<sys/socket.h>

#include	<netlink/netlink.h>
#include	<netlink/netlink_route.h>

#include	<netinet/in.h>
#include	<arpa/inet.h>

#include	<assert.h>
#include	<errno.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdio.h>
#include	<unistd.h>

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

static int	nl_fetch_interfaces(void);
static int	nl_fetch_addresses(void);

nlsocket_t *
nlsocket_create(int flags) {
nlsocket_t	*ret = NULL;
int		 err, optval;
socklen_t	 optlen;

	if ((ret = calloc(1, sizeof(*ret))) == NULL)
		goto err;

	if ((ret->ns_fd = socket(AF_NETLINK, SOCK_RAW | flags,
				 NETLINK_ROUTE)) == -1)
		goto err;

        optval = 1;
        optlen = sizeof(optval);
	if (setsockopt(ret->ns_fd, SOL_NETLINK, NETLINK_MSG_INFO, &optval,
		       optlen) != 0)
		goto err;

	return ret;

err:
	err = errno;

	if (ret && ret->ns_fd)
		close(ret->ns_fd);

	free(ret);
	errno = err;
	return NULL;
}

void
nlsocket_close(nlsocket_t *nls) {
	close(nls->ns_fd);
	free(nls);
}

int
nl_setup(struct kq *kq) {
struct nlsocket	*nls = NULL;
int		 nl_groups[] = {
	RTNLGRP_LINK,
	RTNLGRP_NEIGH,
	RTNLGRP_NEXTHOP,
	RTNLGRP_IPV4_IFADDR,
	RTNLGRP_IPV4_ROUTE,
	RTNLGRP_IPV6_IFADDR,
	RTNLGRP_IPV6_ROUTE,
};

	if ((nls = nlsocket_create(SOCK_NONBLOCK | SOCK_CLOEXEC)) == NULL) {
		nlog(NLOG_FATAL, "nl_setup: nlsocket_create: %s",
		     strerror(errno));
		goto err;
	}

        for (unsigned i = 0; i < sizeof(nl_groups) / sizeof(*nl_groups); i++) {
	int		optval = nl_groups[i];
	socklen_t	optlen = sizeof(optval);

		if (setsockopt(nls->ns_fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
			       &optval, optlen) == -1) {
			nlog(NLOG_FATAL,
			     "nl_setup: NETLINK_ADD_MEMBERSHIP: %s",
			     strerror(errno));
			goto err;
		}
        }

	if (nl_fetch_interfaces() == -1) {
		nlog(NLOG_FATAL, "nl_setup: nl_fetch_interfaces: %s",
		     strerror(errno));
		goto err;
	}

	if (nl_fetch_addresses() == -1) {
		nlog(NLOG_FATAL, "nl_setup: nl_fetch_addresses: %s",
		     strerror(errno));
		goto err;
	}

	if (kq_register_read(kq, nls->ns_fd, donlread, nls) == -1) {
		nlog(NLOG_FATAL, "nl_setup: kq_register_read: %s",
		     strerror(errno));
		goto err;
	}

	return 0;

err:
	if (nls)
		nlsocket_close(nls);
	
	return -1;
}

int
nlsocket_send(nlsocket_t *nls, struct nlmsghdr *nlmsg, size_t msglen) {
struct iovec	iov;
struct msghdr	msg;

	nlog(NLOG_DEBUG, "nlsocket_send: send fd=%d", nls->ns_fd);

	iov.iov_base = nlmsg;
	iov.iov_len = msglen;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if (sendmsg(nls->ns_fd, &msg, 0) == -1)
		return -1;

	return 0;
}

static struct nlmsghdr *
nlsocket_getmsg(nlsocket_t *nls) {
struct nlmsghdr *hdr = (struct nlmsghdr *)nls->ns_bufp;

	if (!NLMSG_OK(hdr, (int)nls->ns_bufn))
		panic("nlsocket_getmsg: data but no message");

	hdr = (struct nlmsghdr *)nls->ns_bufp;
	nls->ns_bufp += hdr->nlmsg_len;
	nls->ns_bufn -= hdr->nlmsg_len;
	return hdr;
}

struct nlmsghdr *
nlsocket_recv(nlsocket_t *nls) {
	if (!nls->ns_bufn) {
	struct iovec	 iov;
	struct msghdr	 msg;
	ssize_t		 n;

		iov.iov_base = nls->ns_buf;
		iov.iov_len = sizeof(nls->ns_buf);

		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		n = recvmsg(nls->ns_fd, &msg, 0);
		if (n < 0)
			return NULL;

		nls->ns_bufp = nls->ns_buf;
		nls->ns_bufn = (size_t)n;
	}

	return nlsocket_getmsg(nls);
}

/*
 * ask the kernel to report all existing network interfaces.
 */
static int
nl_fetch_interfaces(void) {
nlsocket_t	*nls = NULL;
struct nlmsghdr	 hdr;
struct nlmsghdr	*rhdr = NULL;
int		 ret = 0;

	if ((nls = nlsocket_create(SOCK_CLOEXEC)) == NULL) {
		ret = -1;
		goto done;
	}

	memset(&hdr, 0, sizeof(hdr));
	hdr.nlmsg_len = sizeof(hdr);
	hdr.nlmsg_type = RTM_GETLINK;
	hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

	if (nlsocket_send(nls, &hdr, sizeof(hdr)) == -1) {
		ret = -1;
		goto done;
	}

	while ((rhdr = nlsocket_recv(nls)) != NULL) {
		if (rhdr->nlmsg_type == NLMSG_DONE)
			break;

		assert(rhdr->nlmsg_type == RTM_NEWLINK);
		hdl_rtm_newlink(rhdr);
	}

done:
	if (nls)
		nlsocket_close(nls);

	return ret;
}

/*
 * ask the kernel to report all existing addresses.
 */
static int
nl_fetch_addresses(void) {
nlsocket_t	*nls = NULL;
struct nlmsghdr	 hdr;
struct nlmsghdr	*rhdr = NULL;
int		 ret = 0;

	if ((nls = nlsocket_create(SOCK_CLOEXEC)) == NULL) {
		ret = 1;
		goto done;
	}

	memset(&hdr, 0, sizeof(hdr));
	hdr.nlmsg_len = sizeof(hdr);
	hdr.nlmsg_type = RTM_GETADDR;
	hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

	if (nlsocket_send(nls, &hdr, sizeof(hdr)) == -1) {
		ret = 1;
		goto done;
	}

	while ((rhdr = nlsocket_recv(nls)) != NULL) {
		if (rhdr->nlmsg_type == NLMSG_DONE)
			break;

		assert(rhdr->nlmsg_type == RTM_NEWADDR);
		hdl_rtm_newaddr(rhdr);
	}

done:
	if (nls)
		nlsocket_close(nls);

	return ret;
}

/* handle RTM_NEWLINK */
static void
hdl_rtm_newlink(struct nlmsghdr *nlmsg) {
struct ifinfomsg		*ifinfo = NLMSG_DATA(nlmsg);
char				 ifname[IFNAMSIZ + 1];
struct netlink_newlink_data	 msg;

	nlog(NLOG_DEBUG, "RTM_NEWLINK");

	if (if_indextoname((unsigned)ifinfo->ifi_index, ifname) == NULL) {
		nlog(NLOG_DEBUG, "RTM_NEWLINK: couldn't get name for "
		     "ifindex %d", (int) ifinfo->ifi_index);
		return;
	}

	msg.nl_ifindex = (unsigned)ifinfo->ifi_index;
	msg.nl_ifname = ifname;
	msgbus_post(MSG_NETLINK_NEWLINK, &msg);
}

/* handle RTM_DELLINK */
static void
hdl_rtm_dellink(struct nlmsghdr *nlmsg) {
struct ifinfomsg		*ifinfo = NLMSG_DATA(nlmsg);
struct netlink_dellink_data	 msg;

	msg.dl_ifindex = (unsigned)ifinfo->ifi_index;
	msgbus_post(MSG_NETLINK_DELLINK, &msg);
}

/* handle RTM_NEWADDR */
static void
hdl_rtm_newaddr(struct nlmsghdr *nlmsg) {
struct ifaddrmsg		*ifamsg;
struct rtattr			*attrmsg;
size_t				 attrlen;
struct netlink_newaddr_data	 msg;

	nlog(NLOG_DEBUG, "RTM_NEWADDR");

        ifamsg = NLMSG_DATA(nlmsg);

	msg.na_ifindex = ifamsg->ifa_index;
	msg.na_family = ifamsg->ifa_family;
	msg.na_plen = ifamsg->ifa_prefixlen;

	for (attrmsg = IFA_RTA(ifamsg), attrlen = IFA_PAYLOAD(nlmsg);
	     /* cast to int is required because of how RTA_OK() works */
	     /* TODO: this may be a FreeBSD bug */
	     RTA_OK(attrmsg, (int)attrlen);
	     attrmsg = RTA_NEXT(attrmsg, attrlen)) {
		if (attrmsg->rta_type != IFA_ADDRESS)
			continue;

		msg.na_addr = RTA_DATA(attrmsg);
		msgbus_post(MSG_NETLINK_NEWADDR, &msg);
		return;
        }

	nlog(NLOG_WARNING, "received RTM_NEWADDR without an IFA_ADDRESS");
}

/* handle RTM_DELADDR */
static void
hdl_rtm_deladdr(struct nlmsghdr *nlmsg) {
struct ifaddrmsg		*ifamsg;
struct rtattr			*attrmsg;
size_t				 attrlen;
struct netlink_deladdr_data	 msg;

	nlog(NLOG_DEBUG, "RTM_DELADDR");

        ifamsg = NLMSG_DATA(nlmsg);

	msg.da_ifindex = ifamsg->ifa_index;
	msg.da_family = ifamsg->ifa_family;
	msg.da_plen = ifamsg->ifa_prefixlen;

	for (attrmsg = IFA_RTA(ifamsg), attrlen = IFA_PAYLOAD(nlmsg);
	     /* cast to int is required because of how RTA_OK() works */
	     /* TODO: this may be a FreeBSD bug */
	     RTA_OK(attrmsg, (int)attrlen);
	     attrmsg = RTA_NEXT(attrmsg, attrlen)) {
		if (attrmsg->rta_type != IFA_ADDRESS)
			continue;

		msg.da_addr = RTA_DATA(attrmsg);
		msgbus_post(MSG_NETLINK_DELADDR, &msg);
		return;
        }

	nlog(NLOG_WARNING, "received RTM_DELADDR without an IFA_ADDRESS");
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

static kq_disposition_t
donlread(struct kq *kq, int fd, void *udata) {
nlsocket_t	*nls = udata;
struct nlmsghdr	*nlhdr;

	(void)kq;
	(void)fd;

	while ((nlhdr = nlsocket_recv(nls)) != NULL)
		donlmsg(nlhdr);

	if (errno == EAGAIN)
		return KQ_REARM;
	else
		panic("donlpkt failed: %s", strerror(errno));
}
