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

module;

#include	<sys/types.h>
#include	<sys/socket.h>

#include	<netlink/netlink.h>
#include	<netlink/netlink_route.h>

#include	<netinet/in.h>
#include	<arpa/inet.h>

#include	<cassert>
#include	<cerrno>
#include	<cstdlib>
#include	<cstring>
#include	<cstdio>
#include	<map>
#include	<functional>
#include	<expected>
#include	<coroutine>
#include	<unistd.h>

#include	"netd.hh"

import log;
import kq;
import task;

module netlink;

namespace netd::netlink {

namespace {

/* the netlink reader job */
[[nodiscard]] auto reader(std::unique_ptr<socket>) -> task<void>;

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

auto fetch_interfaces(void) -> std::expected<void, std::error_code>;
auto fetch_addresses(void) -> std::expected<void, std::error_code>;

} // anonymous namespace

/*
 * netlink::socket
 */
socket::~socket() {
	if (ns_fd != -1) {
		::close(ns_fd);
		kq::close(ns_fd);
	}
}

auto socket_create(int flags)
	-> std::expected<std::unique_ptr<socket>, std::error_code> {
int		 optval;
socklen_t	 optlen;

	auto sock = std::make_unique<socket>();

	auto kqfd = kq::socket(AF_NETLINK, SOCK_RAW | flags, NETLINK_ROUTE);
	if (!kqfd)
		return std::unexpected(kqfd.error());

	sock->ns_fd = *kqfd;

	optval = 1;
	optlen = sizeof(optval);
	if (setsockopt(sock->ns_fd, SOL_NETLINK, NETLINK_MSG_INFO,
		       &optval, optlen) != 0)
		return std::unexpected(std::make_error_code(std::errc(errno)));

	return sock;
}

auto init(void) -> std::expected<void, std::error_code> {
int		 nl_groups[] = {
	RTNLGRP_LINK,
	RTNLGRP_NEIGH,
	RTNLGRP_NEXTHOP,
	RTNLGRP_IPV4_IFADDR,
	RTNLGRP_IPV4_ROUTE,
	RTNLGRP_IPV6_IFADDR,
	RTNLGRP_IPV6_ROUTE,
};

	auto nls = socket_create(SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (!nls) {
		log::fatal("netlink::init: socket_create: {}",
			   nls.error().message());
		return std::unexpected(nls.error());
	}

	for (unsigned i = 0; i < sizeof(nl_groups) / sizeof(*nl_groups); i++) {
	int		optval = nl_groups[i];
	socklen_t	optlen = sizeof(optval);

		if (setsockopt((*nls)->ns_fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
			       &optval, optlen) == -1) {
			log::fatal("netlink::init: NETLINK_ADD_MEMBERSHIP: {}",
				   strerror(errno));
			return std::unexpected(
				std::make_error_code(std::errc(errno)));
		}
	}

	if (auto ret = fetch_interfaces(); !ret) {
		log::fatal("netlink::init: fetch_interfaces: {}",
			   ret.error().message());
		return std::unexpected(ret.error());
	}

	if (auto ret = fetch_addresses(); !ret) {
		log::fatal("netlink::init: fetch_addresses: {}",
			   ret.error().message());
		return std::unexpected(ret.error());
	}

	(*nls)->ns_bufp = &(*nls)->ns_buf[0];

	kq::run_task(reader(std::move(*nls)));
	return {};
}

namespace {

/*
 * reader: read and process new data from the netlink socket.
 */

auto read_message(socket &nls, std::size_t nbytes)
	-> std::expected<void, std::error_code>
{
	nls.ns_bufn += nbytes;
	auto hdr = (nlmsghdr *)nls.ns_bufp;

	while (NLMSG_OK(hdr, (int)nls.ns_bufn)) {
		log::debug("nlhdr len={} type={} flags={} seq={} pid={}",
		     hdr->nlmsg_len,
		     hdr->nlmsg_type,
		     hdr->nlmsg_flags,
		     hdr->nlmsg_seq,
		     hdr->nlmsg_pid);

		if (auto hdl = handlers.find(hdr->nlmsg_type);
		    hdl != handlers.end())
			hdl->second(hdr);

		nls.ns_bufn -= hdr->nlmsg_len;
		nls.ns_bufp += hdr->nlmsg_len;
		hdr = (nlmsghdr *)nls.ns_bufp;
	}

	/*
	 * we only read full messages, so if we have any data left, something
	 * went wrong with the protocol handling
	 */
	if (nls.ns_bufn > 0)
		panic("netlink::read_message: data left in buffer");

	return {};
}

auto reader(std::unique_ptr<socket> nls) -> task<void> {
	for (;;) {
		auto ret = co_await kq::recvmsg(nls->ns_fd, nls->ns_buf);
		if (!ret)
			panic("netlink::reader: read error: {}",
			      ret.error().message());

		if (!*ret)
			panic("netlink::reader: EOF");

		if (auto r = read_message(*nls, *ret); !r)
			panic("netlink::reader: read_message failed: {}",
			      r.error().message());
	}

	co_return;
}

auto socket_getmsg(socket &nls) -> std::expected<nlmsghdr *, std::error_code> {
nlmsghdr	*hdr = (nlmsghdr *)nls.ns_bufp;

	if (!NLMSG_OK(hdr, (int)nls.ns_bufn))
		return std::unexpected(
			std::make_error_code(std::errc(ENOMSG)));

	hdr = (struct nlmsghdr *)nls.ns_bufp;
	nls.ns_bufp += hdr->nlmsg_len;
	nls.ns_bufn -= hdr->nlmsg_len;
	return hdr;
}

} // anonymous namespace

auto socket_recv(socket &nls)
	-> std::expected<nlmsghdr *, std::error_code>
{
	if (!nls.ns_bufn) {
	ssize_t		n;

		n = read(nls.ns_fd, &nls.ns_buf[0], nls.ns_buf.size());
		if (n < 0)
			return std::unexpected(
				std::make_error_code(std::errc(errno)));

		nls.ns_bufp = &nls.ns_buf[0];
		nls.ns_bufn = (size_t)n;
	}

	return socket_getmsg(nls);
}

auto socket_send(socket &nls, nlmsghdr *nlmsg, size_t msglen)
	-> std::expected<void, std::error_code>
{
iovec	iov;
msghdr	msg;

	log::debug("netlink::socket_send: send fd={}", nls.ns_fd);

	iov.iov_base = nlmsg;
	iov.iov_len = msglen;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if (::sendmsg(nls.ns_fd, &msg, 0) == -1)
		return std::unexpected(std::make_error_code(std::errc(errno)));

	return {};
}

namespace {

/*
 * ask the kernel to report all existing network interfaces.
 */
auto fetch_interfaces(void) -> std::expected<void, std::error_code> {
nlmsghdr	 hdr;

	auto ret = socket_create(SOCK_CLOEXEC);
	if (!ret)
		return std::unexpected(ret.error());
	auto &nls = *ret;

	memset(&hdr, 0, sizeof(hdr));
	hdr.nlmsg_len = sizeof(hdr);
	hdr.nlmsg_type = RTM_GETLINK;
	hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

	if (auto ret = socket_send(*nls, &hdr, sizeof(hdr)); !ret)
		return std::unexpected(ret.error());

	for (;;) {
		auto ret = socket_recv(*nls);
		if (!ret)
			return std::unexpected(ret.error());
		auto rhdr = *ret;

		if (rhdr->nlmsg_type == NLMSG_DONE)
			break;

		assert(rhdr->nlmsg_type == RTM_NEWLINK);
		hdl_rtm_newlink(rhdr);
	}

	return {};
}

/*
 * ask the kernel to report all existing addresses.
 */
auto fetch_addresses(void) -> std::expected<void, std::error_code> {
nlmsghdr	 hdr;

	auto ret = socket_create(SOCK_CLOEXEC);
	if (!ret)
		return std::unexpected(ret.error());
	auto &nls = *ret;

	memset(&hdr, 0, sizeof(hdr));
	hdr.nlmsg_len = sizeof(hdr);
	hdr.nlmsg_type = RTM_GETADDR;
	hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

	if (auto ret = socket_send(*nls, &hdr, sizeof(hdr)); !ret)
		return std::unexpected(ret.error());

	for (;;) {
		auto ret = socket_recv(*nls);
		if (!ret)
			return std::unexpected(ret.error());
		auto rhdr = *ret;

		if (rhdr->nlmsg_type == NLMSG_DONE)
			break;

		assert(rhdr->nlmsg_type == RTM_NEWADDR);
		hdl_rtm_newaddr(rhdr);
	}

	return {};
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
		log::error("RTM_NEWLINK: no interface name?");
		return;
	}

	log::debug("RTM_NEWLINK: {}<{}> nlmsg_flags={:#x} ifi_flags={:#x}"
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

	log::debug("RTM_NEWADDR");

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

	log::warning("received RTM_NEWADDR without an IFA_ADDRESS");
}

/* handle RTM_DELADDR */
void
hdl_rtm_deladdr(nlmsghdr *nlmsg) {
ifaddrmsg	*ifamsg;
rtattr		*attrmsg;
size_t		 attrlen;
deladdr_data	 msg;

	log::debug("RTM_DELADDR");

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

	log::warning("received RTM_DELADDR without an IFA_ADDRESS");
}

} // anonymous namespace

} // namespace netd::netlink
