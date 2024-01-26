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

import log;
import kq;
import task;
import panic;
import netd.error;

module netlink;

namespace netd::netlink {

/* the netlink reader job */
[[nodiscard]] auto reader(socket) -> jtask<void>;

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

auto fetch_interfaces(void) -> task<std::expected<void, std::error_code>>;
auto fetch_addresses(void) -> task<std::expected<void, std::error_code>>;

auto socket::create(int flags)
	-> std::expected<socket, std::error_code> {
int		 optval;
socklen_t	 optlen;

	socket sock;

	sock._fd = ::socket(AF_NETLINK, SOCK_RAW | flags, NETLINK_ROUTE);
	if (sock._fd == -1)
		return std::unexpected(error::from_errno());

	optval = 1;
	optlen = sizeof(optval);
	if (setsockopt(sock._fd, SOL_NETLINK, NETLINK_MSG_INFO,
		       &optval, optlen) != 0)
		return std::unexpected(error::from_errno());

	return sock;
}

auto init(void) -> task<std::expected<void, std::error_code>> {
	auto nls = socket::create(SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (!nls) {
		log::fatal("netlink::init: socket_create: {}",
			   nls.error().message());
		co_return std::unexpected(nls.error());
	}

	auto groups = std::array{
		RTNLGRP_LINK,
		RTNLGRP_NEIGH,
		RTNLGRP_NEXTHOP,
		RTNLGRP_IPV4_IFADDR,
		RTNLGRP_IPV4_ROUTE,
		RTNLGRP_IPV6_IFADDR,
		RTNLGRP_IPV6_ROUTE,
	};

	for (auto group : groups) {
		if (auto ret = co_await nls->join(group); !ret) {
			log::fatal("netlink init: failed to join {}: {}",
				   static_cast<int>(group),
				   ret.error().message());
			co_return std::unexpected(ret.error());
		}
	}

	if (auto ret = co_await fetch_interfaces(); !ret) {
		log::fatal("netlink::init: fetch_interfaces: {}",
			   ret.error().message());
		co_return std::unexpected(ret.error());
	}

	if (auto ret = co_await fetch_addresses(); !ret) {
		log::fatal("netlink::init: fetch_addresses: {}",
			   ret.error().message());
		co_return std::unexpected(ret.error());
	}

	kq::run_task(reader(std::move(*nls)));
	co_return {};
}

auto socket::join(int group) -> task<std::expected<void, std::error_code>> {
	auto optlen = socklen_t(sizeof(group));

	if (setsockopt(_fd,
		       SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
		       &group, optlen) == -1) {
		co_return std::unexpected(error::from_errno());
	}

	co_return {};
}

auto socket::read(void) -> task<std::expected<nlmsghdr *, std::error_code>> {
	log::debug("netlink read: starting");

	// how much to read each go
	constexpr std::size_t blksz = 8192;

	// if we already read a message, discard it from the buffer
	if (_msgsize) {
		_buffer.erase(_buffer.begin(),
			      _buffer.begin() +
				static_cast<ssize_t>(_msgsize));
		_msgsize = 0;
	}

	auto hdr = reinterpret_cast<nlmsghdr *>(&_buffer[0]);

	// keep reading until we got a message
	while (!NLMSG_OK(hdr, static_cast<int>(_pending))) {
		log::debug("netlink read: no message");

		// make sure we have at least blksz bytes left in the buffer
		if (_buffer.size() - _pending < blksz)
			_buffer.resize(_buffer.size() + blksz);

		// read some data and see if it turns into a valid message
		auto r = co_await kq::read(_fd,
					   std::span(_buffer)
						.subspan(_pending));

		if (!r)
			co_return std::unexpected(error::from_errno());

		if (!*r)
			// TODO: consider a better error code here
			co_return std::unexpected(error::from_errno(ENOMSG));

		_pending += *r;
		hdr = reinterpret_cast<nlmsghdr *>(&_buffer[0]);
	}

	log::debug("netlink read: returning");
	_msgsize = hdr->nlmsg_len;

	co_return hdr;
}

auto socket::send(nlmsghdr *nlmsg)
	-> task<std::expected<void, std::error_code>>
{
	log::debug("netlink send: send fd={}", _fd);

	auto bytes = std::span(reinterpret_cast<std::byte const *>(nlmsg),
			       nlmsg->nlmsg_len);
	auto nbytes = co_await kq::write(_fd, bytes);
	if (!nbytes)
		co_return std::unexpected(nbytes.error());

	if (*nbytes < bytes.size())
		panic("netlink send: short write");

	co_return {};
}

/*
 * reader: read and process new data from the netlink socket.
 */

auto reader(socket nls) -> jtask<void> {
	for (;;) {
		auto msg = co_await nls.read();
		if (!msg)
			panic("netlink::reader: read error: {}",
			      msg.error().message());

		if (auto hdl = handlers.find((*msg)->nlmsg_type);
		    hdl != handlers.end())
			hdl->second(*msg);
	}

	co_return;
}

/*
 * ask the kernel to report all existing network interfaces.
 */
auto fetch_interfaces(void) -> task<std::expected<void, std::error_code>> {
nlmsghdr	 hdr;

	auto nls = socket::create(SOCK_CLOEXEC | SOCK_NONBLOCK);
	if (!nls)
		co_return std::unexpected(nls.error());

	memset(&hdr, 0, sizeof(hdr));
	hdr.nlmsg_len = sizeof(hdr);
	hdr.nlmsg_type = RTM_GETLINK;
	hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

	if (auto ret = co_await nls->send(&hdr); !ret)
		co_return std::unexpected(ret.error());

	for (;;) {
		log::debug("fetch_interfaces: reading");
		auto ret = co_await nls->read();
		if (!ret)
			co_return std::unexpected(ret.error());
		auto rhdr = *ret;

		if (rhdr->nlmsg_type == NLMSG_DONE)
			break;

		assert(rhdr->nlmsg_type == RTM_NEWLINK);
		hdl_rtm_newlink(rhdr);
	}

	co_return {};
}

/*
 * ask the kernel to report all existing addresses.
 */
auto fetch_addresses(void) -> task<std::expected<void, std::error_code>> {
nlmsghdr	 hdr;

	auto nls = socket::create(SOCK_CLOEXEC | SOCK_NONBLOCK);
	if (!nls)
		co_return std::unexpected(nls.error());

	memset(&hdr, 0, sizeof(hdr));
	hdr.nlmsg_len = sizeof(hdr);
	hdr.nlmsg_type = RTM_GETADDR;
	hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

	if (auto ret = co_await nls->send(&hdr); !ret)
		co_return std::unexpected(ret.error());

	for (;;) {
		auto ret = co_await nls->read();
		if (!ret)
			co_return std::unexpected(ret.error());
		auto rhdr = *ret;

		if (rhdr->nlmsg_type == NLMSG_DONE)
			break;

		assert(rhdr->nlmsg_type == RTM_NEWADDR);
		hdl_rtm_newaddr(rhdr);
	}

	co_return {};
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

	msg.nl_ifindex = ifinfo->ifi_index;
	msg.nl_flags = ifinfo->ifi_flags;
	evt_newlink.dispatch(msg);
}

/* handle RTM_DELLINK */
void
hdl_rtm_dellink(nlmsghdr *nlmsg) {
ifinfomsg	*ifinfo = static_cast<ifinfomsg *>(NLMSG_DATA(nlmsg));
dellink_data	 msg;

	msg.dl_ifindex = ifinfo->ifi_index;
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

	msg.na_ifindex = static_cast<int>(ifamsg->ifa_index);
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

	msg.da_ifindex = static_cast<int>(ifamsg->ifa_index);
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

} // namespace netd::netlink
