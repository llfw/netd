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

#include <sys/types.h>
#include <sys/event.h>
#include <sys/queue.h>
#include <sys/socket.h>

// clang-format off
#include <netinet/in.h>
#include <netinet/if_ether.h>
// clang-format on

#include <netlink/netlink.h>
#include <netlink/route/common.h>
#include <netlink/route/interface.h>
#include <netlink/route/route.h>

#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <coroutine>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <map>
#include <new>
#include <string>

#include "defs.hh"
#include "generator.hh"

import log;
import kq;
import netlink;
import task;
import netd.util;
import netd.rate;
import netd.uuid;

module iface;

namespace netd::iface {

/* how often to calculate interface stats, in seconds */
constexpr unsigned intf_state_interval = 5;

/* how many previous periods to store; 6 * 5 = 30 seconds */
constexpr unsigned intf_state_history = 6;

using interface_rate = rate<std::uint64_t, intf_state_history>;

/*
 * an interface.  this represents an interface which is active on the system
 * right now.
 */
struct interface {
	interface() = default;
	interface(interface const &) = delete;
	interface(interface &&) = default;
	auto operator=(interface const &) -> interface & = delete;
	auto operator=(interface &&) -> interface & = default;

	uuid		      if_uuid = {};
	std::string	      if_name;
	int		      if_index = 0;
	uint8_t		      if_operstate = 0;
	uint32_t	      if_flags = 0;
	std::vector<ifaddr *> if_addrs;
	/* stats history */
	interface_rate if_obytes;
	interface_rate if_ibytes;
};

isam::isam<interface> interfaces;

isam::index<interface, std::string_view> interfaces_byname(
	interfaces,
	[](interface const &intf) -> std::string_view { return intf.if_name; });

isam::index<interface, uuid> interfaces_byuuid(interfaces,
					       [](interface const &intf) {
						       return intf.if_uuid;
					       });

isam::index<interface, int> interfaces_byindex(interfaces,
					       [](interface const &intf) {
						       return intf.if_index;
					       });

std::uint64_t generation = 0;

/* add a new interface */
auto add_intf(interface &&net) -> interface &
{
	auto it = interfaces.insert(interfaces.end(), std::move(net));
	// we don't need to increment generation here because adding a new
	// interface doesn't invalidate handles
	return *it;
}

/* fetch an interface given a handle */
auto getbyhandle(handle const &h) -> interface &
{
	if (h.ih_gen == generation)
		return *h.ih_ptr;

	if (auto intf = interfaces_byuuid.find(h.ih_uuid);
	    intf != interfaces_byuuid.end()) {
		h.ih_gen = generation;
		return *h.ih_ptr;
	}

	panic("iface: bad handle");
}

/* create a new handle to an existing interface */
auto make_handle(interface *intf) -> handle
{
	auto h = handle();
	h.ih_ptr = intf;
	h.ih_uuid = intf->if_uuid;
	h.ih_gen = generation;
	return h;
}

/* public fetch functions */

auto _getbyname(std::string_view name) noexcept
	-> std::expected<interface *, std::error_code>
{
	if (auto intf = interfaces_byname.find(name);
	    intf != interfaces_byname.end())
		return &*intf->second;

	return std::unexpected(error::from_errno(ESRCH));
}

auto getbyname(std::string_view name) noexcept
	-> std::expected<handle, std::error_code>
{
	auto intf = _getbyname(name);
	if (intf)
		return make_handle(*intf);
	return std::unexpected(intf.error());
}

auto _getbyindex(int index) noexcept
	-> std::expected<interface *, std::error_code>
{
	if (auto intf = interfaces_byindex.find(index);
	    intf != interfaces_byindex.end())
		return &*intf->second;

	return std::unexpected(error::from_errno(ESRCH));
}

auto getbyindex(int index) noexcept -> std::expected<handle, std::error_code>
{
	auto intf = _getbyindex(index);
	if (intf)
		return make_handle(*intf);
	return std::unexpected(intf.error());
}

auto _getbyuuid(uuid id) noexcept -> std::expected<interface *, std::error_code>
{
	if (auto intf = interfaces_byuuid.find(id);
	    intf != interfaces_byuuid.end())
		return &*intf->second;

	return std::unexpected(error::from_errno(ESRCH));
}

auto getbyuuid(uuid id) noexcept -> std::expected<handle, std::error_code>
{
	auto intf = _getbyuuid(id);
	if (intf)
		return make_handle(*intf);
	return std::unexpected(intf.error());
}

auto remove(int index) noexcept -> void
{
	auto intf = interfaces_byindex.find(index);
	if (intf == interfaces_byindex.end())
		panic("iface: removing non-existent index {}", index);
	interfaces.erase(intf->second);
}

auto info(handle const &hdl) noexcept -> ifinfo
{
	auto &intf = getbyhandle(hdl);

	ifinfo info;
	info.name = intf.if_name;
	info.uuid = intf.if_uuid;
	info.index = intf.if_index;
	info.operstate = intf.if_operstate;
	info.flags = intf.if_flags;
	info.rx_bps = intf.if_ibytes.get() * 8;
	info.tx_bps = intf.if_obytes.get() * 8;

	return info;
}

auto getall(void) noexcept -> std::generator<handle>
{
	for (auto &&intf: interfaces)
		co_yield make_handle(&intf);
}

/* netlink event handlers */

void	   hdl_newlink(netlink::newlink_data) noexcept;
event::sub newlink_sub;

void	   hdl_dellink(netlink::dellink_data) noexcept;
event::sub dellink_sub;

void	   hdl_newaddr(netlink::newaddr_data) noexcept;
event::sub newaddr_sub;

void	   hdl_deladdr(netlink::deladdr_data) noexcept;
event::sub deladdr_sub;

/* stats update timer */
auto stats(void) -> jtask<void>;

auto init(void) noexcept -> int
{
	newlink_sub = event::sub(netlink::evt_newlink, hdl_newlink);
	dellink_sub = event::sub(netlink::evt_dellink, hdl_dellink);
	newaddr_sub = event::sub(netlink::evt_newaddr, hdl_newaddr);
	deladdr_sub = event::sub(netlink::evt_deladdr, hdl_deladdr);

	kq::run_task(stats());
	return 0;
}

auto hdl_newlink(netlink::newlink_data msg) noexcept -> void
{
	/* check for duplicate interfaces */
	for (auto &&intf: interfaces) {
		if (intf.if_name == msg.nl_ifname
		    || intf.if_index == msg.nl_ifindex) {
			return;
		}
	}

	interface intf;
	intf.if_index = msg.nl_ifindex;
	intf.if_name = msg.nl_ifname;
	intf.if_flags = msg.nl_flags;
	intf.if_operstate = msg.nl_operstate;

	log::info("{}<{}>: new interface", intf.if_name, intf.if_index);

	interfaces.insert(interfaces.end(), std::move(intf));
}

auto hdl_dellink(netlink::dellink_data msg) noexcept -> void
{
	auto intf = getbyindex(msg.dl_ifindex);
	if (!intf)
		log::warning("hdl_dellink: missing ifindex {}?",
			     msg.dl_ifindex);

	auto iff = info(*intf);
	log::info("{}<{}>: interface destroyed", iff.name, iff.index);

	remove(iff.index);
}

ifaddr *ifaddr_new(int family, void *addr, int plen) noexcept
{
	ifaddr *ret = NULL;

	if (plen < 0)
		goto err;

	if ((ret = new (std::nothrow) ifaddr) == NULL)
		goto err;

	switch (family) {
	case AF_INET: {
		if (plen > 32)
			goto err;

		in_addr addr4;
		std::memcpy(&addr4, addr, sizeof(struct in_addr));
		ret->ifa_addr = addr4;
		ret->ifa_family = AF_INET;
		ret->ifa_plen = plen;
		break;
	}

	case AF_INET6: {
		if (plen > 128)
			goto err;

		in6_addr addr6;
		std::memcpy(&addr6, addr, sizeof(struct in6_addr));
		ret->ifa_addr = addr6;
		ret->ifa_family = AF_INET6;
		ret->ifa_plen = plen;
		break;
	}

	case AF_LINK: {
		/* Ethernet addresses don't have a mask */
		if (plen != 48)
			goto err;

		ether_addr eaddr;
		std::memcpy(&eaddr, addr, sizeof(struct ether_addr));
		ret->ifa_addr = eaddr;
		ret->ifa_family = AF_LINK;
		ret->ifa_plen = plen;
		break;
	}
	}

	return ret;

err:
	if (ret)
		delete ret;

	return NULL;
}

auto hdl_newaddr(netlink::newaddr_data msg) noexcept -> void
{
	auto ret = _getbyindex(msg.na_ifindex);
	if (!ret)
		return;
	auto &intf = *ret;

	auto *addr = ifaddr_new(msg.na_family, msg.na_addr, msg.na_plen);
	if (addr == NULL)
		/* unsupported family, etc. */
		return;

	log::info("{}<{}>: address added", intf->if_name, intf->if_index);

	intf->if_addrs.push_back(addr);
}

auto hdl_deladdr(netlink::deladdr_data msg) noexcept -> void
{
	/* TODO: implement */

	auto ret = _getbyindex(msg.da_ifindex);
	if (!ret)
		return;
	auto &intf = *ret;

	log::info("{}<{}>: address removed", intf->if_name, intf->if_index);
}

/* calculate interface stats */

void ifdostats(interface &intf, rtnl_link_stats64 *ostats) noexcept
{
	rtnl_link_stats64 stats;

	/* copy out stats since netlink can misalign it */
	std::memcpy(&stats, ostats, sizeof(stats));

	intf.if_obytes.update(stats.tx_bytes);
	intf.if_ibytes.update(stats.rx_bytes);
}

auto stats_update(void) -> task<void>
{
	struct nlmsghdr hdr;

	log::debug("iface: running stats");

	auto nls = netlink::socket::create(SOCK_CLOEXEC | SOCK_NONBLOCK);
	if (!nls) {
		log::error("stats: netlink::socket_create: {}",
			   nls.error().message());
		co_return;
	}

	/* ask the kernel to send us interface stats */
	memset(&hdr, 0, sizeof(hdr));
	hdr.nlmsg_len = sizeof(hdr);
	hdr.nlmsg_type = RTM_GETLINK;
	hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

	if (auto ret = co_await nls->send(&hdr); !ret) {
		log::error("stats: netlink send: {}", ret.error().message());
		co_return;
	}

	/* read the interface details */
	for (;;) {
		auto ret = co_await nls->read();
		if (!ret) {
			log::error("stats: netlink read: {}",
				   ret.error().message());
			co_return;
		}

		if (*ret == nullptr)
			break;

		auto	   rhdr = *ret;
		ifinfomsg *ifinfo = NULL;
		rtattr	  *attrmsg = NULL;
		size_t	   attrlen;

		if (rhdr->nlmsg_type == NLMSG_DONE)
			break;

		if (rhdr->nlmsg_type != RTM_NEWLINK)
			continue;

		ifinfo = static_cast<ifinfomsg *>(NLMSG_DATA(rhdr));

		auto intf_ = _getbyindex(ifinfo->ifi_index);
		if (!intf_) {
			log::error("stats: missing interface {}?",
				   ifinfo->ifi_index);
			continue;
		}
		auto &intf = *intf_;

		for (attrmsg = IFLA_RTA(ifinfo), attrlen = IFLA_PAYLOAD(rhdr);
		     RTA_OK(attrmsg, (int)attrlen);
		     attrmsg = RTA_NEXT(attrmsg, attrlen)) {

			switch (attrmsg->rta_type) {
			case IFLA_STATS64:
				ifdostats(*intf,
					  static_cast<rtnl_link_stats64 *>(
						  RTA_DATA(attrmsg)));
				break;
			}
		}
	}
}

auto stats() -> jtask<void>
{
	using namespace std::literals;

	for (;;) {
		co_await kq::sleep(1s * intf_state_interval);
		co_await stats_update();
	}
}

} // namespace netd::iface
