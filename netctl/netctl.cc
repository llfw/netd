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
#include	<sys/un.h>
#include	<sys/nv.h>

extern "C" { // TODO: file upstream bug
#include	<libxo/xo.h>
}

#include	<ranges>
#include	<expected>
#include	<algorithm>
#include	<iostream>
#include	<print>
#include	<format>
#include	<functional>
#include	<set>
#include	<map>
#include	<span>
#include	<vector>

#include	<cassert>
#include	<cerrno>
#include	<cstdio>
#include	<cstdlib>
#include	<cstring>
#include	<cinttypes>
#include	<unistd.h>

#include	"defs.hh"

import nvl;
import xo;
import netd.proto;

using namespace std::literals;

namespace netd {

int	netd_connect(void);

using cmdhandler = std::function<int (int server,
				      std::span<std::string const> args)>;

namespace {

auto	c_intf_list(int, std::span<std::string const>) -> int;
auto	c_net_list(int, std::span<std::string const>) -> int;
auto	c_net_create(int, std::span<std::string const>) -> int;
auto	c_net_delete(int, std::span<std::string const>) -> int;

struct command {
	using cmdmap = std::map<std::string_view, command>;

	command(cmdhandler handler, std::string_view description)
	: cm_handler(handler)
	, cm_description(description) {}

	command(std::map<std::string_view, command> const &subs,
		std::string_view description)
	: cm_subs(subs)
	, cm_description(description) {}

	/*
	 * find a matching sub-command in this command's subs.
	 */
	auto match(std::string_view name) const
		-> std::expected<command const *, std::string>
	{
		auto last = cm_subs.lower_bound(name);
		if (last == cm_subs.end())
			return std::unexpected("unknown command"s);

		last = std::next(last);
		auto first = last;

		while (first != cm_subs.begin()
		       && std::prev(first)->first.substr(0, name.size()) == name)
			--first;

		if (std::distance(first, last) == 1)
			return &first->second;
		else if (std::distance(first, last) == 0)
			return std::unexpected("unknown command"s);
		else
			return std::unexpected("ambiguous command"s);
	}

	cmdhandler		cm_handler;
	cmdmap			cm_subs;
	std::string_view	cm_description;
};

command::cmdmap intf_cmds{
	{ "list"sv,	command(c_intf_list, "list interfaces"sv) },
};

command::cmdmap net_cmds{
	{ "list"sv,	command(c_net_list, "list networks"sv) },
	{ "create"sv,	command(c_net_create, "create new network"sv) },
	{ "delete"sv,	command(c_net_delete, "delete existing network"sv) },
};

command::cmdmap root_cmds{
	{ "interface"sv,	command(intf_cmds,
					"configure layer 2 interfaces"sv) },
	{ "network"sv,		command(net_cmds,
					"configure layer 3 networks"sv ) },
};

command root_cmd(root_cmds, ""sv);

/*
 * send the given command to the server and return the response.
 */
auto nv_xfer(int server, nvl const &cmd)
	-> std::expected<nvl, std::error_code>
{
ssize_t		 n;
struct iovec	 iov;
struct msghdr	 mhdr;

	/* make sure the nvlist is not errored */
	if (auto error = cmd.error(); error)
		return std::unexpected(*error);

	/* send the command */

	auto cmdbuf = cmd.pack();
	if (!cmdbuf)
		return std::unexpected(cmdbuf.error());

	iov.iov_base = cmdbuf->data();
	iov.iov_len = cmdbuf->size();

	std::memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;

	n = ::sendmsg(server, &mhdr, MSG_EOR);
	if (n == -1)
		return std::unexpected(std::make_error_code(std::errc(errno)));

	/* read the response */

	std::vector<std::byte> respbuf(proto::max_msg_size);

	iov.iov_base = respbuf.data();
	iov.iov_len = respbuf.size();

	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;

	n = ::recvmsg(server, &mhdr, 0);
	if (n == -1)
		return std::unexpected(std::make_error_code(std::errc(errno)));

	if (n == 0) {
		std::print(stderr, "{}: empty reply from server\n",
			getprogname());
		return std::unexpected(std::make_error_code(std::errc(EINVAL)));
	}

	if (!(mhdr.msg_flags & MSG_EOR))
		return std::unexpected(std::make_error_code(std::errc(EINVAL)));

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
auto send_simple_command(int server, std::string_view command)
	-> std::expected<nvl, std::error_code>
{
	auto cmd = nvl();
	cmd.add_string(proto::cp_cmd, command);
	if (auto error = cmd.error(); error)
		return std::unexpected(*error);

	return nv_xfer(server, cmd);
}

void
usage(command const &root) {
	std::print(stderr, "usage: {} [--libxo=...] <command>\n",
		   getprogname());
	std::print(stderr, "\n");
	std::print(stderr, "commands:\n");
	std::print(stderr, "\n");

	for (auto &&cmd: root.cm_subs)
		std::print(stderr, "  {:<20} {}\n",
			   cmd.first,
			   cmd.second.cm_description);
}

int
c_intf_list(int server, std::span<std::string const> args)
{
	xo::xo xo_guard;
	xo::container intf_container("interface-list");

	if (!args.empty()) {
		xo::emit("{E/usage: %s interface list}\n",
			getprogname());
		return 1;
	}

	auto resp = send_simple_command(server, proto::cc_getifs);
	if (!resp) {
		xo::emit("{E:/%s: failed to send command: %s\n}",
			getprogname(), resp.error().message());
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

		if (!intf.exists_string(proto::cp_iface_name) ||
		    !intf.exists_number(proto::cp_iface_admin) ||
		    !intf.exists_number(proto::cp_iface_oper) ||
		    !intf.exists_number(proto::cp_iface_txrate) ||
		    !intf.exists_number(proto::cp_iface_rxrate)) {
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
		}

		xo::instance intf_instance("interface");
		xo::emit("{V:name/%-16s}"
			"{V:admin-state/%-6s}"
			"{V:oper-state/%-5s}"
			"{[:8}{Vhn,hn-decimal,hn-1000:txrate/%ju}b/s{]:}"
			"{[:8}{Vhn,hn-decimal,hn-1000:rxrate/%ju}b/s{]:}"
			"\n",
			intf.get_string(proto::cp_iface_name),
			adminstate, operstate,
			intf.get_number(proto::cp_iface_txrate),
			intf.get_number(proto::cp_iface_rxrate));
	}

	return 0;
}

auto c_net_list(int server, std::span<std::string const> args)
	-> int
{
	xo::xo xo_guard;
	xo::container net_container("network-list");

	if (!args.empty()) {
		xo::emit("{E/usage: %s network list}\n", getprogname());
		return 1;
	}

	auto resp = send_simple_command(server, proto::cc_getnets);
	if (!resp) {
		xo::emit("{E:/%s: failed to send command: %s\n}",
			getprogname(), resp.error().message());
		return 1;
	}

	if (!resp->exists_nvlist_array(proto::cp_nets))
		/* no networks configured */
		return 0;

	xo::emit("{T:NAME/%-16s}\n");

	for (auto &&net : resp->get_nvlist_array(proto::cp_nets)) {
		if (!net.exists_string(proto::cp_net_name)) {
			xo::emit("{E:/%s: invalid response}\n", getprogname());
			return 1;
		}

		xo::instance net_instance("network");
		xo::emit("{V:name/%-16s}\n",
			 net.get_string(proto::cp_net_name));
	}

	return 0;
}

auto c_net_create(int server, std::span<std::string const> args)
	-> int
{
	xo::xo xo_guard;

	if (args.size() != 1) {
		xo::emit("{E/usage: %s network create <name>}\n",
			getprogname());
		return 1;
	}

	nvl cmd;

	cmd.add_string(proto::cp_cmd, proto::cc_newnet);
	cmd.add_string(proto::cp_newnet_name, args[0]);

	if (auto error = cmd.error(); error) {
		xo::emit("{E:/%s: nvlist: %s\n}",
			getprogname(), error->message());
		return 1;
	}

	auto resp = nv_xfer(server, cmd);

	if (!resp) {
		xo::emit("{E:/%s: failed to send command: %s\n}",
			getprogname(), resp.error().message());
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

auto c_net_delete(int server, std::span<std::string const> args) -> int {
	xo::xo xo_guard;

	if (args.size() != 1) {
		xo::emit("{E/usage: %s network delete <name>}\n",
			 getprogname());
		return 1;
	}

	nvl cmd;

	cmd.add_string(proto::cp_cmd, proto::cc_delnet);
	cmd.add_string(proto::cp_delnet_name, args[0]);

	if (auto error = cmd.error(); error) {
		xo::emit("{E:/%s: nvlist: %s\n}",
			 getprogname(), error->message());
		return 1;
	}

	auto resp = nv_xfer(server, cmd);

	if (!resp) {
		xo::emit("{E:/%s: failed to send command: %s\n}",
			 getprogname(), resp.error().message());
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

} // anonymous namespace

auto find_command(command const &root,
		  std::vector<std::string> &args)
	-> std::optional<command const *>
{
	if (args.empty()) {
		std::print(stderr, "incomplete command\n");
		usage(root);
		return {};
	}

	auto const *cur = &root;

	while (!args.empty()) {
		auto match = cur->match(args[0]);

		if (!match) {
			std::print(stderr, "{}\n", match.error());
			return {};
		}

		cur = *match;

		if (cur->cm_handler) {
			args.erase(args.begin());
			return cur;
		}

		if (args.size() == 1) {
			std::print(stderr, "{}: incomplete command\n",
				   args[0]);
			return {};
		}

		args.erase(args.begin());
	}

	return {};
}

int
netd_connect(void) {
int			sock = -1, i;
struct sockaddr_un	sun;

	sock = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (sock == -1) {
		perror("socket");
		goto err;
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	assert(proto::socket_path.size() < sizeof(sun.sun_path));
	std::ranges::copy(proto::socket_path, &sun.sun_path[0]);

	i = connect(sock, (struct sockaddr *)&sun, (socklen_t)SUN_LEN(&sun));
	if (i == -1) {
		perror("connect");
		goto err;
	}

	return sock;

err:
	if (sock != -1)
		close(sock);
	return -1;
}

} // namespace netd

auto main(int argc, char **argv) -> int {
int	server;

	setprogname(argv[0]);

	argc = xo_parse_args(argc, argv);
	if (argc < 0)
		return EXIT_FAILURE;

	--argc;
	++argv;

	auto args = std::span(argv, argv + argc)
		    | std::views::transform([](auto &&s) {
			    return std::string(s);
		    })
		    | std::ranges::to<std::vector>();

	if (args.empty()) {
		usage(netd::root_cmd);
		return 1;
	}

	auto cmd = netd::find_command(netd::root_cmd, args);
	if (!cmd)
		return 1;

	if ((server = netd::netd_connect()) == -1)
		return 1;

	return (*cmd)->cm_handler(server, args);
}
