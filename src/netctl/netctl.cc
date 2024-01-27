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

#include <sys/types.h>
#include <sys/nv.h>
#include <sys/socket.h>
#include <sys/un.h>

extern "C" { // TODO: file upstream bug
#include <libxo/xo.h>
}

#include <algorithm>
#include <expected>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <vector>

#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "defs.hh"

import netd.nvl;
import netd.xo;
import netd.proto;
import netd.util;

using namespace std::literals;

namespace netd {

auto netd_connect() noexcept -> int;

using cmdhandler =
	std::function<int(int server, std::span<std::string_view const> args)>;

auto c_intf_list(int server, std::span<std::string_view const> args) noexcept
	-> int;
auto c_net_list(int server, std::span<std::string_view const> args) noexcept
	-> int;
auto c_net_create(int server, std::span<std::string_view const> args) noexcept
	-> int;
auto c_net_delete(int server, std::span<std::string_view const> args) noexcept
	-> int;

struct command {
	using cmdmap = std::map<std::string_view, command>;

	command(std::string_view description, cmdhandler handler) noexcept
		: cm_handler(std::move(handler)), cm_description(description)
	{
	}

	command(std::string_view description, cmdmap &&subs) noexcept
	try : cm_subs(std::move(subs)), cm_description(description) {
	} catch (...) {
		abort();
	}

	/*
	 * find a matching sub-command in this command's subs.
	 */
	[[nodiscard]] auto match(std::string_view name) const noexcept
		-> std::expected<command const *, std::string>
	{
		auto last = cm_subs.lower_bound(name);
		if (last == cm_subs.end())
			return std::unexpected("unknown command"s);

		last = std::next(last);
		auto first = last;

		while (first != cm_subs.begin()
		       && std::prev(first)->first.substr(0, name.size())
				  == name)
			--first;

		if (std::distance(first, last) == 1)
			return &first->second;
		if (std::distance(first, last) == 0)
			return std::unexpected("unknown command"s);
		return std::unexpected("ambiguous command"s);
	}

	cmdhandler	 cm_handler;
	cmdmap		 cm_subs;
	std::string_view cm_description;
};

/*
 * send the given command to the server and return the response.
 */
auto nv_xfer(int server, nvl const &cmd) noexcept
	-> std::expected<nvl, std::error_code>
{
	/* make sure the nvlist is not errored */
	if (auto error = cmd.error(); error)
		return std::unexpected(*error);

	/* send the command */

	auto cmdbuf = cmd.pack();
	if (!cmdbuf)
		return std::unexpected(cmdbuf.error());

	auto iov = iovec{data(*cmdbuf), size(*cmdbuf)};

	msghdr mhdr{};
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;

	auto n = ::sendmsg(server, &mhdr, MSG_EOR);
	if (n == -1)
		return std::unexpected(std::make_error_code(std::errc(errno)));

	/* read the response */

	auto respbuf = std::vector<std::byte>();
	try {
		respbuf.resize(proto::max_msg_size);
	} catch (...) {
		abort();
	}

	iov.iov_base = respbuf.data();
	iov.iov_len = respbuf.size();

	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;

	n = ::recvmsg(server, &mhdr, 0);
	if (n == -1)
		return std::unexpected(error::from_errno());

	if (n == 0) {
		(void)print(stderr, "{}: empty reply from server\n",
			    getprogname());
		return std::unexpected(error::from_errno(ENOMSG));
	}

	if ((mhdr.msg_flags & MSG_EOR) == 0)
		return std::unexpected(error::from_errno(ENOMSG));

	/* unpack and return the response */
	auto resp = nvl::unpack(respbuf | std::views::take(n));
	if (!resp)
		return std::unexpected(resp.error());

	return *resp;
}

/*
 * send a simple command (with no arguments) to the server and return the
 * response.
 */
auto send_simple_command(int server, std::string_view command) noexcept
	-> std::expected<nvl, std::error_code>
{
	auto cmd = nvl();
	cmd.add_string(proto::cp_cmd, command);
	if (auto error = cmd.error(); error)
		return std::unexpected(*error);

	return nv_xfer(server, cmd);
}

void usage(command const &root) noexcept
{
	(void)print(stderr, "usage: {} [--libxo=...] <command>\n",
		    getprogname());
	(void)print(stderr, "\n");
	(void)print(stderr, "commands:\n");
	(void)print(stderr, "\n");

	for (auto &&cmd: root.cm_subs)
		(void)print(stderr, "  {:<20} {}\n", cmd.first,
			    cmd.second.cm_description);
}

auto c_intf_list(int server, std::span<std::string_view const> args) noexcept
	-> int
{
	auto xo_guard = xo::xo();
	auto intf_container = xo::container("interface-list");

	if (!args.empty()) {
		xo::emit("{E/usage: %s interface list}\n", getprogname());
		return 1;
	}

	auto resp = send_simple_command(server, proto::cc_getifs);
	if (!resp) {
		xo::emit("{E:/%s: failed to send command: %s\n}", getprogname(),
			 resp.error().message());
		return 1;
	}

	if (!resp->exists_nvlist_array(proto::cp_iface)) {
		/* no interfaces available */
		xo::emit("{E:no interfaces configured}\n");
		return 0;
	}

	xo::emit("{T:NAME/%-16s}{T:ADMIN/%-6s}{T:OPER/%-5s}"
		 "{T:TX/%8s}{T:RX/%8s}\n");

	for (auto &&intf: resp->get_nvlist_array(proto::cp_iface)) {
		auto operstate = "UNK"sv, adminstate = "UNK"sv;

		if (!intf.exists_string(proto::cp_iface_name)
		    || !intf.exists_number(proto::cp_iface_admin)
		    || !intf.exists_number(proto::cp_iface_oper)
		    || !intf.exists_number(proto::cp_iface_txrate)
		    || !intf.exists_number(proto::cp_iface_rxrate)) {
			xo::emit("{E:/%s: invalid response}\n", getprogname());
			return 1;
		}

		switch (intf.get_number(proto::cp_iface_admin)) {
		case proto::cv_iface_admin_up:
			adminstate = "UP"sv;
			break;

		case proto::cv_iface_admin_down:
			adminstate = "DOWN"sv;
			break;

		default:
			break;
		}

		switch (intf.get_number(proto::cp_iface_oper)) {
		case proto::cv_iface_oper_not_present:
			operstate = "NOHW"sv;
			break;
		case proto::cv_iface_oper_down:
			operstate = "DOWN"sv;
			break;
		case proto::cv_iface_oper_lower_down:
			operstate = "LDWN"sv;
			break;
		case proto::cv_iface_oper_testing:
			operstate = "TEST"sv;
			break;
		case proto::cv_iface_oper_dormant:
			operstate = "DRMT"sv;
			break;
		case proto::cv_iface_oper_up:
			operstate = "UP"sv;
			break;
		default:
			break;
		}

		auto intf_instance = xo::instance("interface");
		xo::emit("{V:name/%-16s}"
			 "{V:admin-state/%-6s}"
			 "{V:oper-state/%-5s}"
			 "{[:8}{Vhn,hn-decimal,hn-1000:txrate/%ju}b/s{]:}"
			 "{[:8}{Vhn,hn-decimal,hn-1000:rxrate/%ju}b/s{]:}"
			 "\n",
			 intf.get_string(proto::cp_iface_name), adminstate,
			 operstate, intf.get_number(proto::cp_iface_txrate),
			 intf.get_number(proto::cp_iface_rxrate));
	}

	return 0;
}

auto c_net_list(int server, std::span<std::string_view const> args) noexcept
	-> int
{
	auto xo_guard = xo::xo();
	auto net_container = xo::container("network-link");

	if (!args.empty()) {
		xo::emit("{E/usage: %s network list}\n", getprogname());
		return 1;
	}

	auto resp = send_simple_command(server, proto::cc_getnets);
	if (!resp) {
		xo::emit("{E:/%s: failed to send command: %s\n}", getprogname(),
			 resp.error().message());
		return 1;
	}

	if (!resp->exists_nvlist_array(proto::cp_nets))
		/* no networks configured */
		return 0;

	xo::emit("{T:NAME/%-16s}\n");

	for (auto &&net: resp->get_nvlist_array(proto::cp_nets)) {
		if (!net.exists_string(proto::cp_net_name)) {
			xo::emit("{E:/%s: invalid response}\n", getprogname());
			return 1;
		}

		auto net_instance = xo::instance("network");
		xo::emit("{V:name/%-16s}\n",
			 net.get_string(proto::cp_net_name));
	}

	return 0;
}

auto c_net_create(int server, std::span<std::string_view const> args) noexcept
	-> int
{
	auto xo_guard = xo::xo();

	if (args.size() != 1) {
		xo::emit("{E/usage: %s network create <name>}\n",
			 getprogname());
		return 1;
	}

	nvl cmd;

	cmd.add_string(proto::cp_cmd, proto::cc_newnet);
	cmd.add_string(proto::cp_newnet_name, args[0]);

	if (auto error = cmd.error(); error) {
		xo::emit("{E:/%s: nvlist: %s\n}", getprogname(),
			 error->message());
		return 1;
	}

	auto resp = nv_xfer(server, cmd);

	if (!resp) {
		xo::emit("{E:/%s: failed to send command: %s\n}", getprogname(),
			 resp.error().message());
		return 1;
	}

	if (!resp->exists_string(proto::cp_status)) {
		xo::emit("{E:/%s: invalid response}", getprogname());
		return 1;
	}

	auto status = resp->get_string(proto::cp_status);

	if (status == proto::cv_status_success)
		return 0;

	/* we got an error */
	if (!resp->exists_string(proto::cp_status_info)) {
		xo::emit("{E:/%s: invalid response}", getprogname());
		return 1;
	}

	xo::emit("{E:/%s}\n", resp->get_string(proto::cp_status));
	return 1;
}

auto c_net_delete(int server, std::span<std::string_view const> args) noexcept
	-> int
{
	auto xo_guard = xo::xo();

	if (args.size() != 1) {
		xo::emit("{E/usage: %s network delete <name>}\n",
			 getprogname());
		return 1;
	}

	nvl cmd;

	cmd.add_string(proto::cp_cmd, proto::cc_delnet);
	cmd.add_string(proto::cp_delnet_name, args[0]);

	if (auto error = cmd.error(); error) {
		xo::emit("{E:/%s: nvlist: %s\n}", getprogname(),
			 error->message());
		return 1;
	}

	auto resp = nv_xfer(server, cmd);

	if (!resp) {
		xo::emit("{E:/%s: failed to send command: %s\n}", getprogname(),
			 resp.error().message());
		return 1;
	}

	if (!resp->exists_string(proto::cp_status)) {
		xo::emit("{E:/%s: invalid response}", getprogname());
		return 1;
	}

	auto status = resp->get_string(proto::cp_status);

	if (status == proto::cv_status_success)
		return 0;

	/* we got an error */
	if (!resp->exists_string(proto::cp_status_info)) {
		xo::emit("{E:/%s: invalid response}", getprogname());
		return 1;
	}

	xo::emit("{E:/%s}\n", resp->get_string(proto::cp_status));
	return 1;
}

auto find_command(command const			&root,
		  std::vector<std::string_view> &args) noexcept
	-> std::optional<command const *>
{
	if (args.empty()) {
		(void)print(stderr, "incomplete command\n");
		usage(root);
		return {};
	}

	auto const *cur = &root;

	while (!args.empty()) {
		auto match = cur->match(args[0]);

		if (!match) {
			(void)print(stderr, "{}\n", match.error());
			return {};
		}

		cur = *match;

		if (cur->cm_handler) {
			args.erase(args.begin());
			return cur;
		}

		if (args.size() == 1) {
			(void)print(stderr, "{}: incomplete command\n",
				    args[0]);
			return {};
		}

		args.erase(args.begin());
	}

	return {};
}

auto netd_connect() noexcept -> int
{

	auto sock = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (sock == -1) {
		perror("socket");
		return -1;
	}

	auto sun = sockaddr_un{};
	sun.sun_family = AF_UNIX;
	static_assert(proto::socket_path.size() < sizeof(sun.sun_path));
	std::ranges::copy(proto::socket_path, &sun.sun_path[0]);

	auto i = connect(sock, reinterpret_cast<sockaddr *>(&sun),
			 static_cast<socklen_t>(SUN_LEN(&sun)));
	if (i == -1) {
		perror("connect");
		::close(sock);
		return -1;
	}

	return sock;
}

} // namespace netd

auto main(int argc, char **argv) -> int
try {
	using namespace netd;

	setprogname(argv[0]);

	argc = xo_parse_args(argc, argv);
	if (argc < 0)
		return EXIT_FAILURE;

	--argc;
	++argv;

	auto args = std::span(argv, argv + argc)
		  | std::views::transform(
			    [](auto &&s) { return std::string_view(s); })
		  | std::ranges::to<std::vector>();

	// clang-format off
	auto root_cmd = command("netctl"sv, command::cmdmap{
{"interface"sv, command("configure layer 2 interfaces"sv,
	 command::cmdmap{
		 {"list"sv, command("list interfaces"sv,
				    c_intf_list)}})
},
{"network"sv, command("configure layer 3 networks"sv,
	 command::cmdmap{
		 {"list"sv, command("list networks"sv,
				    c_net_list)},
		 {"create"sv, command("create new network"sv,
				      c_net_create)},
		 {"delete"sv, command("delete existing network"sv,
				      c_net_delete)},
	 })}
});
	// clang-format on

	if (args.empty()) {
		usage(root_cmd);
		return 1;
	}

	auto cmd = find_command(root_cmd, args);
	if (!cmd)
		return 1;

	auto server = netd_connect();
	if (server == -1)
		return 1;

	return (*cmd)->cm_handler(server, args);
} catch (std::exception const &exc) {
	(void)netd::panic("unhandled exception: {}", exc.what());
} catch (...) {
	(void)netd::panic("unhandled exception");
}
