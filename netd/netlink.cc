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

#include	<map>
#include	<functional>

#include	"netlink.hh"
#include	"netd.hh"
#include	"kq.hh"
#include	"log.hh"

namespace netlink {

namespace {

auto nldomsg(int fd, ssize_t nbytes, socket *nonnull) -> kq::disp;

void	hdl_rtm_newlink(struct nlmsghdr *nonnull);
void	hdl_rtm_dellink(struct nlmsghdr *nonnull);
void	hdl_rtm_newaddr(struct nlmsghdr *nonnull);
void	hdl_rtm_deladdr(struct nlmsghdr *nonnull);

std::map<int, std::function<void (nlmsghdr *nonnull)>> handlers = {
	{ RTM_NEWLINK, hdl_rtm_newlink },
	{ RTM_DELLINK, hdl_rtm_dellink },
	{ RTM_NEWADDR, hdl_rtm_newaddr },
	{ RTM_DELADDR, hdl_rtm_deladdr },
};

int	nl_fetch_interfaces(void);
int	nl_fetch_addresses(void);

} // anonymous namespace

socket *
socket_create(int flags) {
socket		*ret = NULL;
int		 err, optval;
socklen_t	 optlen;

	if ((ret = new (std::nothrow) socket) == NULL)
		goto err;

	if ((ret->ns_fd = kq::socket(AF_NETLINK, SOCK_RAW | flags,
				     NETLINK_ROUTE)) == -1)
		goto err;

	optval = 1;
	optlen = sizeof(optval);
	if (setsockopt(ret->ns_fd, SOL_NETLINK, NETLINK_MSG_INFO,
		       &optval, optlen) != 0)
		goto err;

	return ret;

err:
	err = errno;
	if (ret && ret->ns_fd)
		kq::close(ret->ns_fd);
	delete ret;
	errno = err;
	return NULL;
}

void
socket_close(socket *nls) {
	kq::close(nls->ns_fd);
	delete nls;
}

int
init(void) {
struct socket	*nls = NULL;
int		 nl_groups[] = {
	RTNLGRP_LINK,
	RTNLGRP_NEIGH,
	RTNLGRP_NEXTHOP,
	RTNLGRP_IPV4_IFADDR,
	RTNLGRP_IPV4_ROUTE,
	RTNLGRP_IPV6_IFADDR,
	RTNLGRP_IPV6_ROUTE,
};

	nls = socket_create(SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (!nls) {
		nlog::fatal("nlinit: socket_create: {}",
		     strerror(errno));
		goto err;
	}

	for (unsigned i = 0; i < sizeof(nl_groups) / sizeof(*nl_groups); i++) {
	int		optval = nl_groups[i];
	socklen_t	optlen = sizeof(optval);

		if (setsockopt(nls->ns_fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
			       &optval, optlen) == -1) {
			nlog::fatal("nlinit: NETLINK_ADD_MEMBERSHIP: {}",
				    strerror(errno));
			goto err;
		}
	}

	if (nl_fetch_interfaces() == -1) {
		nlog::fatal("nlinit: nl_fetch_interfaces: {}",
			    strerror(errno));
		goto err;
	}

	if (nl_fetch_addresses() == -1) {
		nlog::fatal("nlinit: nl_fetch_addresses: {}",
			    strerror(errno));
		goto err;
	}

	nls->ns_bufp = &nls->ns_buf[0];

	{
		auto handler = [=](int fd, ssize_t nbytes) {
			return nldomsg(fd, nbytes, nls);
		};

		kq::read(nls->ns_fd, std::span(nls->ns_buf), handler);
	}

	return 0;

err:
	if (nls)
		socket_close(nls);

	return -1;
}

namespace {

/*
 * donlread: call when netlink data is received
 */
auto nldomsg(int fd, ssize_t nbytes, socket *nls) -> kq::disp {
nlmsghdr	*hdr;

	(void)fd;

	assert(nls);

	if (nbytes < 0)
		panic("donlread: netlink read error: %s",
		      strerror(-(int)nbytes));

	nls->ns_bufn += (size_t)nbytes;
	hdr = (struct nlmsghdr *)nls->ns_bufp;

	while (NLMSG_OK(hdr, (int)nls->ns_bufn)) {
		nlog::debug("nlhdr len={} type={} flags={} seq={} pid={}",
		     hdr->nlmsg_len,
		     hdr->nlmsg_type,
		     hdr->nlmsg_flags,
		     hdr->nlmsg_seq,
		     hdr->nlmsg_pid);

		if (auto hdl = handlers.find(hdr->nlmsg_type);
		    hdl != handlers.end())
			hdl->second(hdr);

		nls->ns_bufn -= hdr->nlmsg_len;
		nls->ns_bufp += hdr->nlmsg_len;
		hdr = (struct nlmsghdr *)nls->ns_bufp;
	}

	/* shift any pending data back to the start of the buffer */
	if (nls->ns_bufn > 0) {
		memmove(&nls->ns_buf[0], nls->ns_bufp, nls->ns_bufn);
		nls->ns_bufp = &nls->ns_buf[0];
	}

	/*
	 * issue another read here instead of using kq::rearm because we want
	 * to read into the remaining part of the buffer.
	 * TODO: replace this once we have a better kqread().
	 */
	auto handler = [=](int fd, ssize_t nbytes) {
		return nldomsg(fd, nbytes, nls);
	};
	kq::read(nls->ns_fd,
	       std::span(nls->ns_bufp, (sizeof(nls->ns_buf) - nls->ns_bufn)),
	       handler);
	return kq::disp::stop;
}

struct nlmsghdr *
socket_getmsg(socket *nls) {
struct nlmsghdr	*hdr = (struct nlmsghdr *)nls->ns_bufp;

	if (!NLMSG_OK(hdr, (int)nls->ns_bufn))
		panic("netlink::socket_getmsg: data but no message");

	hdr = (struct nlmsghdr *)nls->ns_bufp;
	nls->ns_bufp += hdr->nlmsg_len;
	nls->ns_bufn -= hdr->nlmsg_len;
	return hdr;
}

} // anonymous namespace

struct nlmsghdr *
socket_recv(socket *nls) {
	if (!nls->ns_bufn) {
	ssize_t		n;

		n = read(nls->ns_fd, &nls->ns_buf[0], nls->ns_buf.size());
		if (n < 0)
			return NULL;

		nls->ns_bufp = &nls->ns_buf[0];
		nls->ns_bufn = (size_t)n;
	}

	return socket_getmsg(nls);
}

int
socket_send(socket *nls, struct nlmsghdr *nlmsg, size_t msglen) {
struct iovec	iov;
struct msghdr	msg;

	nlog::debug("netlink::socket_send: send fd={}", nls->ns_fd);

	iov.iov_base = nlmsg;
	iov.iov_len = msglen;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if (sendmsg(nls->ns_fd, &msg, 0) == -1)
		return -1;

	return 0;
}

namespace {

/*
 * ask the kernel to report all existing network interfaces.
 */
int
nl_fetch_interfaces(void) {
socket		*nls = NULL;
nlmsghdr	 hdr;
nlmsghdr	*rhdr = NULL;
int		 ret = 0;

	if ((nls = socket_create(SOCK_CLOEXEC)) == NULL) {
		ret = -1;
		goto done;
	}

	memset(&hdr, 0, sizeof(hdr));
	hdr.nlmsg_len = sizeof(hdr);
	hdr.nlmsg_type = RTM_GETLINK;
	hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

	if (socket_send(nls, &hdr, sizeof(hdr)) == -1) {
		ret = -1;
		goto done;
	}

	while ((rhdr = socket_recv(nls)) != NULL) {
		if (rhdr->nlmsg_type == NLMSG_DONE)
			break;

		assert(rhdr->nlmsg_type == RTM_NEWLINK);
		hdl_rtm_newlink(rhdr);
	}

done:
	if (nls)
		socket_close(nls);

	return ret;
}

/*
 * ask the kernel to report all existing addresses.
 */
int
nl_fetch_addresses(void) {
socket		*nls = NULL;
nlmsghdr	 hdr;
nlmsghdr	*rhdr = NULL;
int		 ret = 0;

	if ((nls = socket_create(SOCK_CLOEXEC)) == NULL) {
		ret = 1;
		goto done;
	}

	memset(&hdr, 0, sizeof(hdr));
	hdr.nlmsg_len = sizeof(hdr);
	hdr.nlmsg_type = RTM_GETADDR;
	hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

	if (socket_send(nls, &hdr, sizeof(hdr)) == -1) {
		ret = 1;
		goto done;
	}

	while ((rhdr = socket_recv(nls)) != NULL) {
		if (rhdr->nlmsg_type == NLMSG_DONE)
			break;

		assert(rhdr->nlmsg_type == RTM_NEWADDR);
		hdl_rtm_newaddr(rhdr);
	}

done:
	if (nls)
		socket_close(nls);

	return ret;
}

/* handle RTM_NEWLINK */
void
hdl_rtm_newlink(struct nlmsghdr *nlmsg) {
ifinfomsg		*ifinfo = static_cast<ifinfomsg *>(NLMSG_DATA(nlmsg));
rtattr			*attrmsg = NULL;
size_t			 attrlen;
newlink_data		 msg;
rtnl_link_stats64	 stats;

	memset(&msg, 0, sizeof(msg));
	memset(&stats, 0, sizeof(stats));

	for (attrmsg = IFLA_RTA(ifinfo), attrlen = IFLA_PAYLOAD(nlmsg);
	     RTA_OK(attrmsg, (int) attrlen);
		attrmsg = RTA_NEXT(attrmsg, attrlen)) {

		switch (attrmsg->rta_type) {
		case IFLA_IFNAME:
			msg.nl_ifname =
				static_cast<char const *>(RTA_DATA(attrmsg));
			break;

		case IFLA_OPERSTATE:
			msg.nl_operstate = *((uint8_t *)RTA_DATA(attrmsg));
			break;

		case IFLA_STATS64:
			/* copy out since netlink can misalign 8-byte values */
			memcpy(&stats, RTA_DATA(attrmsg), sizeof(stats));
			msg.nl_stats = &stats;
			break;
		}
	}

	if (msg.nl_ifname.empty()) {
		nlog::error("RTM_NEWLINK: no interface name?");
		return;
	}

	nlog::debug("RTM_NEWLINK: {}<{}> nlmsg_flags={:#x} ifi_flags={:#x}"
		    "ifi_change={:#x}",
		    msg.nl_ifname,
		    ifinfo->ifi_index,
		    nlmsg->nlmsg_flags,
		    ifinfo->ifi_flags,
		    ifinfo->ifi_change);

	msg.nl_ifindex = (unsigned)ifinfo->ifi_index;
	msg.nl_flags = ifinfo->ifi_flags;
	evt_newlink.dispatch(msg);
}

/* handle RTM_DELLINK */
void
hdl_rtm_dellink(nlmsghdr *nlmsg) {
ifinfomsg	*ifinfo = static_cast<ifinfomsg *>(NLMSG_DATA(nlmsg));
dellink_data	 msg;

	msg.dl_ifindex = (unsigned)ifinfo->ifi_index;
	evt_dellink.dispatch(msg);
}

/* handle RTM_NEWADDR */
void
hdl_rtm_newaddr(nlmsghdr *nlmsg) {
ifaddrmsg	*ifamsg;
rtattr		*attrmsg;
size_t		 attrlen;
newaddr_data	 msg;

	nlog::debug("RTM_NEWADDR");

	ifamsg = static_cast<ifaddrmsg *>(NLMSG_DATA(nlmsg));

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
		evt_newaddr.dispatch(msg);
		return;
	}

	nlog::warning("received RTM_NEWADDR without an IFA_ADDRESS");
}

/* handle RTM_DELADDR */
void
hdl_rtm_deladdr(nlmsghdr *nlmsg) {
ifaddrmsg	*ifamsg;
rtattr		*attrmsg;
size_t		 attrlen;
deladdr_data	 msg;

	nlog::debug("RTM_DELADDR");

	ifamsg = static_cast<ifaddrmsg *>(NLMSG_DATA(nlmsg));

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
		evt_deladdr.dispatch(msg);
		return;
	}

	nlog::warning("received RTM_DELADDR without an IFA_ADDRESS");
}

} // anonymous namespace

} // namespace netlink
