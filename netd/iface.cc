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

#include	<sys/queue.h>
#include	<sys/event.h>

#include	<netlink/netlink.h>
#include	<netlink/route/common.h>
#include	<netlink/route/route.h>

#include	<cerrno>
#include	<cstring>
#include	<cstdlib>
#include	<cinttypes>

#include	<new>

#include	"iface.hh"
#include	"log.hh"
#include	"msgbus.hh"
#include	"netlink.hh"
#include	"netd.hh"
#include	"kq.hh"

namespace netd::iface {

std::map<std::string, interface *nonnull> interfaces;

namespace {

/* netlink event handlers */

void	hdl_newlink(netlink::newlink_data);
msgbus::sub newlink_sub;

void	hdl_dellink(netlink::dellink_data);
msgbus::sub dellink_sub;

void	hdl_newaddr(netlink::newaddr_data);
msgbus::sub newaddr_sub;

void	hdl_deladdr(netlink::deladdr_data);
msgbus::sub deladdr_sub;

/* stats update timer */
auto stats(void) -> task<void>;

} // anonymous namespace

auto init(void) -> int {
	newlink_sub = msgbus::sub(netlink::evt_newlink, hdl_newlink);
	dellink_sub = msgbus::sub(netlink::evt_dellink, hdl_dellink);
	newaddr_sub = msgbus::sub(netlink::evt_newaddr, hdl_newaddr);
	deladdr_sub = msgbus::sub(netlink::evt_deladdr, hdl_deladdr);

	kq::run_task(stats());
	return 0;
}

auto find_interface_byname(std::string_view name) -> interface * {
	if (auto it = interfaces.find(std::string(name));
	    it != interfaces.end())
		return it->second;

	errno = ESRCH;
	return NULL;
}

interface *
find_interface_byindex(unsigned int ifindex) {
	for (auto &&[name, iface] : interfaces)
		if (iface->if_index == ifindex)
			return iface;

	errno = ESRCH;
	return NULL;
}

namespace {

auto hdl_newlink(netlink::newlink_data msg) -> void {
interface		*intf;

	/* check for duplicate interfaces */
	for (auto [name, intf]: interfaces) {
		if (intf->if_name == msg.nl_ifname ||
		    intf->if_index == msg.nl_ifindex) {
			return;
		}
	}

	if ((intf = new (std::nothrow) interface) == NULL)
		panic("hdl_newlink: out of memory");

	intf->if_index = msg.nl_ifindex;
	intf->if_name = msg.nl_ifname;
	intf->if_flags = msg.nl_flags;
	intf->if_operstate = msg.nl_operstate;

	/*
	 * Set all the stats history entries to the current value to avoid
	 * calculating a huge statistic when the interface first appears.
	 */
	for (unsigned i = 0; i < intf_state_history; ++i) {
		intf->if_obytes[i] = msg.nl_stats->tx_bytes;
		intf->if_ibytes[i] = msg.nl_stats->rx_bytes;
	}

	log::info("{}<{}>: new interface", intf->if_name, intf->if_index);

	interfaces.insert({intf->if_name, intf});
}

auto hdl_dellink(netlink::dellink_data msg) -> void {
	for (auto &&it : interfaces) {
		if (it.second->if_index != msg.dl_ifindex)
			continue;

		log::info("{}<{}>: interface destroyed",
			  it.second->if_name,
			  it.second->if_index);

		interfaces.erase(it.first);
		return;
	}

	log::warning("hdl_dellink: missing ifindex {}?", msg.dl_ifindex);
}

} // anonymous namespace

ifaddr *
ifaddr_new(int family, void *addr, int plen) {
ifaddr	*ret = NULL;

	if (plen < 0)
		goto err;

	if ((ret = new (std::nothrow) ifaddr) == NULL)
		goto err;

	switch (family) {
	case AF_INET:
		if (plen > 32)
			goto err;

		ret->ifa_family = AF_INET;
		memcpy(&ret->ifa_addr4, addr, sizeof(struct in_addr));
		ret->ifa_plen = plen;
		break;

	case AF_INET6:
		if (plen > 128)
			goto err;

		ret->ifa_family = AF_INET6;
		memcpy(&ret->ifa_addr6, addr, sizeof(struct in6_addr));
		ret->ifa_plen = plen;
		break;

	case AF_LINK:
		/* Ethernet addresses don't have a mask */
		if (plen != 48)
			goto err;

		ret->ifa_family = AF_LINK;
		memcpy(&ret->ifa_ether, addr, sizeof(struct ether_addr));
		ret->ifa_plen = plen;
		break;
	}

	return ret;

err:
	if (ret)
		delete ret;

	return NULL;
}

namespace {

auto hdl_newaddr(netlink::newaddr_data msg) -> void {
interface	*intf;
ifaddr		*addr;

	if ((intf = find_interface_byindex(msg.na_ifindex)) == NULL)
		return;

	addr = ifaddr_new(msg.na_family, msg.na_addr, msg.na_plen);
	if (addr == NULL)
		/* unsupported family, etc. */
		return;

	log::info("{}<{}>: address added", intf->if_name, intf->if_index);

	intf->if_addrs.push_back(addr);
}

auto hdl_deladdr(netlink::deladdr_data msg) -> void {
interface		*intf;
	/* TODO: implement */

	if ((intf = find_interface_byindex(msg.da_ifindex)) == NULL)
		return;

	log::info("{}<{}>: address removed", intf->if_name, intf->if_index);
}

} // anonymous namespace

/* calculate interface stats */

static void
ifdostats(interface *intf, struct rtnl_link_stats64 *ostats) {
uint64_t			n, i;
struct rtnl_link_stats64	stats;

	/* TODO: make this more general */

	/* copy out stats since netlink can misalign it */
	memcpy(&stats, ostats, sizeof(stats));

	/* tx */
	memmove(intf->if_obytes, intf->if_obytes + 1,
		sizeof(*intf->if_obytes) * (intf_state_history - 1));
	intf->if_obytes[intf_state_history - 1] = stats.tx_bytes;
	for (n = 1, i = 0; n < intf_state_history; ++n)
		i += intf->if_obytes[n] - intf->if_obytes[n - 1];
	intf->if_txrate = ((i * 8) / intf_state_history) / intf_state_interval;

	/* rx */
	memmove(intf->if_ibytes, intf->if_ibytes + 1,
		sizeof(*intf->if_ibytes) * (intf_state_history - 1));
	intf->if_ibytes[intf_state_history - 1] = stats.rx_bytes;
	for (n = 1, i = 0; n < intf_state_history; ++n)
		i += intf->if_ibytes[n] - intf->if_ibytes[n - 1];
	intf->if_rxrate = ((i * 8) / intf_state_history) / intf_state_interval;
}

namespace {

auto stats_update(void) -> task<void> {
struct nlmsghdr		 hdr;

	log::debug("iface: running stats");

	auto nlsr = netlink::socket_create(0);
	if (!nlsr) {
		log::error("stats: netlink::socket_create: {}",
			   nlsr.error().message());
		co_return;
	}

	auto &nls = *nlsr;

	/* ask the kernel to send us interface stats */
	memset(&hdr, 0, sizeof(hdr));
	hdr.nlmsg_len = sizeof(hdr);
	hdr.nlmsg_type = RTM_GETLINK;
	hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

	if (auto ret = socket_send(*nls, &hdr, sizeof(hdr)); !ret) {
		log::error("stats: socket_send: {}", ret.error().message());
		co_return;
	}

	/* read the interface details */
	for (;;) {
		auto ret = socket_recv(*nls);
		if (!ret) {
			log::error("stats: socket_recv: {}",
				   ret.error().message());
			co_return;
		}

		if (*ret == nullptr)
			break;

		auto rhdr = *ret;
		ifinfomsg	*ifinfo = NULL;
		rtattr		*attrmsg = NULL;
		size_t		 attrlen;
		interface	*intf = NULL;

		if (rhdr->nlmsg_type == NLMSG_DONE)
			break;

		if (rhdr->nlmsg_type != RTM_NEWLINK)
			continue;

		ifinfo = static_cast<ifinfomsg *>(NLMSG_DATA(rhdr));

		intf = find_interface_byindex((unsigned)ifinfo->ifi_index);
		if (intf == NULL) {
			log::error("stats: missing interface {}?",
				   ifinfo->ifi_index);
			continue;
		}

		for (attrmsg = IFLA_RTA(ifinfo), attrlen = IFLA_PAYLOAD(rhdr);
		     RTA_OK(attrmsg, (int) attrlen);
		     attrmsg = RTA_NEXT(attrmsg, attrlen)) {

			switch (attrmsg->rta_type) {
			case IFLA_STATS64:
				ifdostats(intf,
					static_cast<rtnl_link_stats64 *>(
						RTA_DATA(attrmsg)));
				break;
			}
		}
	}
}

auto stats() -> task<void> {
	using namespace std::literals;

	for (;;) {
		co_await kq::sleep(5s);
		co_await stats_update();
	}
}

} // anonymous namespace

} // namespace netd::iface
