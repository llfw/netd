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
int		 err, optval, optlen;

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
	int	err, optval = nl_groups[i], optlen = sizeof(optval);

		if ((err = setsockopt(nls->ns_fd, SOL_NETLINK,
				      NETLINK_ADD_MEMBERSHIP,
				      &optval, optlen)) != 0) {
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
		nls->ns_bufn = n;
	}

	return nlsocket_getmsg(nls);
}

/*
 * ask the kernel to report all existing network interfaces.
 */
static int
nl_fetch_interfaces(void) {
nlsocket_t	*nls;
struct nlmsghdr	 hdr;
struct nlmsghdr	*rhdr;

	if ((nls = nlsocket_create(SOCK_CLOEXEC)) == NULL)
		return -1;

	memset(&hdr, 0, sizeof(hdr));
	hdr.nlmsg_len = sizeof(hdr);
	hdr.nlmsg_type = RTM_GETLINK;
	hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

	if (nlsocket_send(nls, &hdr, sizeof(hdr)) == -1)
		return -1;

	while ((rhdr = nlsocket_recv(nls)) != NULL) {
		if (rhdr->nlmsg_type == NLMSG_DONE)
			break;

		assert(rhdr->nlmsg_type == RTM_NEWLINK);
		nlog(NLOG_DEBUG, "nl_fetch_interfaces: recv");
		nlog(NLOG_DEBUG, "nlhdr len   = %u", (unsigned int) rhdr->nlmsg_len);
		nlog(NLOG_DEBUG, "nlhdr type  = %u", (unsigned int) rhdr->nlmsg_type);
		nlog(NLOG_DEBUG, "nlhdr flags = %u", (unsigned int) rhdr->nlmsg_flags);
		nlog(NLOG_DEBUG, "nlhdr seq   = %u", (unsigned int) rhdr->nlmsg_seq);
		nlog(NLOG_DEBUG, "nlhdr pid   = %u", (unsigned int) rhdr->nlmsg_pid);
		hdl_rtm_newlink(rhdr);
	}

	return 0;
}

/*
 * ask the kernel to report all existing addresses.
 */
static int
nl_fetch_addresses(void) {
nlsocket_t	*nls;
struct nlmsghdr	 hdr;
struct nlmsghdr	*rhdr;

	if ((nls = nlsocket_create(SOCK_CLOEXEC)) == NULL)
		return -1;

	memset(&hdr, 0, sizeof(hdr));
	hdr.nlmsg_len = sizeof(hdr);
	hdr.nlmsg_type = RTM_GETADDR;
	hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

	if (nlsocket_send(nls, &hdr, sizeof(hdr)) == -1)
		return -1;

	while ((rhdr = nlsocket_recv(nls)) != NULL) {
		if (rhdr->nlmsg_type == NLMSG_DONE)
			break;

		assert(rhdr->nlmsg_type == RTM_NEWADDR);
		nlog(NLOG_DEBUG, "nl_fetch_addresses: recv");
		nlog(NLOG_DEBUG, "nlhdr len   = %u", (unsigned int) rhdr->nlmsg_len);
		nlog(NLOG_DEBUG, "nlhdr type  = %u", (unsigned int) rhdr->nlmsg_type);
		nlog(NLOG_DEBUG, "nlhdr flags = %u", (unsigned int) rhdr->nlmsg_flags);
		nlog(NLOG_DEBUG, "nlhdr seq   = %u", (unsigned int) rhdr->nlmsg_seq);
		nlog(NLOG_DEBUG, "nlhdr pid   = %u", (unsigned int) rhdr->nlmsg_pid);
		hdl_rtm_newaddr(rhdr);
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
struct ifaddrmsg	*ifamsg;
struct rtattr		*attrmsg;
interface_t		*intf;
int			 attrlen;

	nlog(NLOG_DEBUG, "RTM_NEWADDR");

        ifamsg = NLMSG_DATA(nlmsg);
        attrmsg = IFA_RTA(ifamsg);
        attrlen = IFA_PAYLOAD(nlmsg);

	if ((intf = find_interface_byindex(ifamsg->ifa_index)) == NULL) {
		nlog(NLOG_WARNING,
		     "RTM_NEWADDR received for unknown ifindex %d",
		     ifamsg->ifa_index);
		return;
	}

        while (RTA_OK(attrmsg, attrlen)) {
	ifaddr_t	*newaddr;
		if (attrmsg->rta_type == IFA_ADDRESS) {
			switch (ifamsg->ifa_family) {
			case AF_INET:
				newaddr = ifaddr_new(AF_INET,
						     RTA_DATA(attrmsg),
						     ifamsg->ifa_prefixlen);
				break;

			case AF_INET6:
				newaddr = ifaddr_new(AF_INET6,
						     RTA_DATA(attrmsg),
						     ifamsg->ifa_prefixlen);
				break;

			default:
				nlog(NLOG_WARNING,
				     "RTM_NEWADDR: unsupported address "
				     "family %d", ifamsg->ifa_family);
				return;
			}

			if (newaddr == NULL)
				panic("RTM_NEWADDR: can't create address");

			interface_address_added(intf, newaddr);
		}

		attrmsg = RTA_NEXT(attrmsg, attrlen);
        }
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
