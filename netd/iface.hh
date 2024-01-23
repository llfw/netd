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

#ifndef	NETD_IFACE_H_INCLUDED
#define	NETD_IFACE_H_INCLUDED

#include	<sys/types.h>
#include	<sys/socket.h>
#include	<sys/queue.h>
#include	<sys/uuid.h>

#include	<netinet/in.h>
#include	<netinet/if_ether.h>

#include	<map>
#include	<string>
#include	<vector>

#include	"db.hh"
#include	"defs.hh"

namespace netd::iface {

/*
 * manage running interfaces.
 */

auto init(void) -> int;

/* an address assigned to an interface */
struct ifaddr {
	int			ifa_family	= 0;
	union {
		struct ether_addr	ifa_ether;
		struct in_addr		ifa_addr4;
		struct in6_addr		ifa_addr6;
	};
	int			ifa_plen	= 0;	/* prefix length */
};

ifaddr *nullable ifaddr_new(int family, void *nonnull addr, int plen);

/*
 * an interface.  this represents an interface which is active on the system
 * right now.
 */

/* how often to calculate interface stats, in seconds */
constexpr unsigned intf_state_interval = 5;
/* how many previous periods to store; 6 * 5 = 30 seconds */
constexpr unsigned intf_state_history = 6;

struct network;

struct interface {
	interface() = default;
	interface(interface const &) = delete;
	interface(interface &&) = default;
	auto operator=(interface const &) -> interface& = delete;
	auto operator=(interface &&) -> interface& = default;

	uuid			if_uuid = {};
	std::string		if_name;
	unsigned int		if_index = 0;
	uint8_t			if_operstate = 0;
	uint32_t		if_flags = 0;
	std::vector<ifaddr *>	if_addrs;
	network *nullable	if_network = nullptr;
	pinterface *nullable	if_pintf = nullptr;
	/* stats history */
	uint64_t		if_obytes[intf_state_history] = {};
	uint64_t		if_txrate = 0; /* bps */
	uint64_t		if_ibytes[intf_state_history] = {};
	uint64_t		if_rxrate = 0; /* bps */
};

extern std::map<std::string, interface *nonnull> interfaces;

/* find an existing interface */
auto find_interface_byname(std::string_view name) -> interface *nullable;
auto find_interface_byindex(unsigned int ifindex) -> interface *nullable;

} // namespace netd::iface

#endif	/* !NETD_IFACE_H_INCLUDED */
