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

#include	<string>
#include	<array>
#include	<expected>
#include	<memory>
#include	<system_error>

#include	"msgbus.hh"
#include	"defs.hh"

namespace netd::netlink {

/*
 * maximum size of a message we can read from netlink.
 * TODO: remove this limit once we have kqaread().
 */
constexpr unsigned socket_bufsize = 32 * 1024;

struct socket {
	socket() = default;
	socket(socket &&) = default;
	socket(socket const &) = delete;

	auto operator=(socket const &) = delete;
	auto operator=(socket &&) -> socket& = default;

	~socket();

	std::array<std::byte, socket_bufsize>
				ns_buf = {};
	int			ns_fd = -1;
	std::byte * nonnull	ns_bufp = &ns_buf[0];
	size_t			ns_bufn = 0;
};

/* initialise the netlink subsystem */
auto init(void) -> std::expected<void, std::error_code>;

/* create a new rtnetlink socket */
auto socket_create(int flags)
	-> std::expected<std::unique_ptr<socket>, std::error_code>;

/* send a message on a netlink socket */
auto socket_send(socket &, nlmsghdr *nonnull msg, size_t msglen)
	-> std::expected<void, std::error_code>;

/* receive a message on a netlink socket */
auto socket_recv(socket &) -> std::expected<nlmsghdr *, std::error_code>;

/*
 * msgbus events
 */

/* interface created */
struct newlink_data {
	unsigned		nl_ifindex;
	std::string		nl_ifname;
	uint8_t			nl_operstate;
	uint32_t		nl_flags;
	struct rtnl_link_stats64 * nonnull
				nl_stats;
};

inline msgbus::event<newlink_data> evt_newlink;

/* interface destroyed */
struct dellink_data {
	unsigned	 dl_ifindex;
};
inline msgbus::event<dellink_data> evt_dellink;

/* interface address created */
struct newaddr_data {
	unsigned	na_ifindex;
	int		na_family;
	int		na_plen;
	void * nonnull	na_addr;
};
inline msgbus::event<newaddr_data> evt_newaddr;


/* interface address removed */
struct deladdr_data {
	unsigned	da_ifindex;
	int		da_family;
	int		da_plen;
	void * nonnull	da_addr;
};
inline msgbus::event<deladdr_data> evt_deladdr;

} // namespace netd::netlink

#endif	/* !NETD_NETLINK_H_INCLUDED */
