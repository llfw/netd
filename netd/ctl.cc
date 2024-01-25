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
#include	<sys/un.h>
#include	<sys/nv.h>

#include	<netlink/netlink.h>
#include	<netlink/route/interface.h>

#include	<net/if.h>

#include	<cassert>
#include	<cerrno>
#include	<cstdlib>
#include	<cstring>
#include	<expected>
#include	<functional>
#include	<coroutine>
#include	<unistd.h>

#include	<new>
#include	<print>

module ctl;

import network;
import nvl;
import log;
import kq;
import iface;
import task;
import panic;
import netd.proto;
import netd.error;

namespace netd::ctl {

struct ctlclient {
	ctlclient(int fd_) : fd(fd_) {}

	ctlclient(ctlclient const &) = delete;
	ctlclient(ctlclient &&) = default;
	~ctlclient() {
		::close(fd);
	}

	auto operator=(ctlclient const &) = delete;
	auto operator=(ctlclient &&) -> ctlclient& = default;

	int	fd;
	std::array<std::byte, proto::max_msg_size>
		buf = {};
};

using cmdhandler = std::function<task<void> (ctlclient &, nvl const &)>;

struct chandler {
	std::string_view	ch_cmd;
	cmdhandler		ch_handler;
};

[[nodiscard]] auto send_error(ctlclient &, std::string_view) -> task<void>;
[[nodiscard]] auto send_success(ctlclient &, std::string_view = {}) -> task<void>;
[[nodiscard]] auto send_syserr(ctlclient &, std::string_view) -> task<void>;

/*
 * command handlers
 */

[[nodiscard]] auto h_intf_list(ctlclient &, nvl const &) -> task<void>;
[[nodiscard]] auto h_net_create(ctlclient &, nvl const &) -> task<void>;
[[nodiscard]] auto h_net_delete(ctlclient &, nvl const &) -> task<void>;
[[nodiscard]] auto h_net_list(ctlclient &, nvl const &) -> task<void>;

std::vector<chandler> chandlers{
	{ proto::cc_getifs,	h_intf_list	},
	{ proto::cc_getnets,	h_net_list	},
	{ proto::cc_newnet,	h_net_create	},
	{ proto::cc_delnet,	h_net_delete	},
};

[[nodiscard]] auto listener(int fd) -> jtask<void>;
[[nodiscard]] auto client_handler(std::unique_ptr<ctlclient>) -> jtask<void>;
[[nodiscard]] auto clientcmd(ctlclient &, nvl const &cmd) -> task<void>;

auto init() -> std::expected<void, std::error_code> {
sockaddr_un	sun;
std::string	path(proto::socket_path); // for unlink

	// TODO: close socket on error
	(void) unlink(path.c_str());

	auto sock = socket(AF_UNIX,
			   SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
			   0);

	if (sock == -1) {
		log::fatal("ctl::init: socket: {}", std::strerror(errno));
		return std::unexpected(error::from_errno());
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	assert(proto::socket_path.size() < sizeof(sun.sun_path));
	std::ranges::copy(proto::socket_path, &sun.sun_path[0]);

	if (bind(sock,
		 (struct sockaddr *)&sun,
		 (socklen_t)SUN_LEN(&sun)) == -1) {
		log::fatal("ctl::init: bind: {}", strerror(errno));
		return std::unexpected(error::from_errno());
	}

	if (listen(sock, 128) == -1) {
		log::fatal("ctl::init: listen: {}", strerror(errno));
		return std::unexpected(error::from_errno());
	}

	kq::run_task(listener(sock));
	log::debug("ctl::init: listening on {}", path);
	return {};
}

auto listener(int sfd) -> jtask<void> {
	for (;;) {
		auto fd = co_await kq::accept4(sfd, nullptr, nullptr,
					       SOCK_NONBLOCK | SOCK_CLOEXEC);
		if (!fd)
			panic("ctl::listener: accept failed: {}",
			      fd.error().message());

		auto client = std::make_unique<ctlclient>(*fd);
		log::debug("acceptclient: new client fd={}", client->fd);

		kq::run_task(client_handler(std::move(client)));
	}

	co_return;
}

/*
 * main task for handling a client.
 */
auto client_handler(std::unique_ptr<ctlclient> client) -> jtask<void> {
	log::debug("client_handler: starting, fd={}", client->fd);

	/* read the command */
	auto nbytes = co_await kq::recvmsg(client->fd, client->buf);
	if (!nbytes) {
		log::warning("client read error: {}",
			     nbytes.error().message());
		co_return;
	}

	if (!*nbytes) {
		log::warning("client disconnected");
		co_return;
	}

	log::debug("client_handler: msg size={}", *nbytes);

	auto msgbytes = std::span(client->buf).subspan(0, *nbytes);

	auto cmd = nvl::unpack(msgbytes, 0);
	if (!cmd) {
		log::debug("readclient: nvl::unpack: {}",
			   cmd.error().message());
		co_return;
	}

	// TODO: is this check necessary?
	if (auto error = cmd->error(); error) {
		log::debug("readclient: nvlist error: {}", error->message());
		co_return;
	}

	co_await clientcmd(*client, *cmd);

	log::debug("client_handler: done");
	co_return;
}

/*
 * handle a command from a client and reply to it.
 */

auto clientcmd(ctlclient &client, nvl const &cmd) -> task<void>
{
	if (!cmd.exists_string(proto::cp_cmd)) {
		log::debug("clientcmd: missing cp_cmd");
		co_return;
	}

	auto cmdname = cmd.get_string(proto::cp_cmd);
	log::debug("clientcmd: cmd={}", std::string(cmdname));

	for (auto handler : chandlers) {
		if (cmdname != handler.ch_cmd)
			continue;

		co_await handler.ch_handler(client, cmd);
		co_return;
	}

	/* TODO: unknown command, send an error */
}

/*
 * send the given response to the client.
 */

auto send_response(ctlclient &client, nvl const &resp) -> task<void> {
msghdr	 mhdr;
iovec	 iov;
ssize_t	 n;

	if (auto error = resp.error(); error) {
		log::debug("send_response: nvlist error: {}",
			   error->message());
		co_return;
	}

	auto rbuf = resp.pack();
	if (!rbuf) {
		log::debug("send_response: nvlist_pack failed: {}",
			   rbuf.error().message());
		co_return;
	}

	iov.iov_base = rbuf->data();
	iov.iov_len = rbuf->size();

	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;

	/* TODO: assume this won't block for now */
	n = ::sendmsg(client.fd, &mhdr, MSG_EOR);
	if (n == -1)
		log::debug("send_response: sendmsg: {}", strerror(errno));
	co_return;
}

/*
 * send a success response to the client, with optional STATUS_INFO.
 */
auto send_success(ctlclient &client, std::string_view info) -> task<void>
{
	auto resp = nvl();

	resp.add_string(proto::cp_status, proto::cv_status_success);
	if (!info.empty())
		resp.add_string(proto::cp_status_info, info);

	if (auto error = resp.error(); error) {
		log::error("send_success: nvl pack error: {}",
			   error->message());
		co_return;
	}

	co_await send_response(client, resp);
}

/*
 * send an error response to the client.
 */
auto send_error(ctlclient &client, std::string_view error) -> task<void> {
	auto resp = nvl();
	resp.add_string(proto::cp_status, proto::cv_status_error);
	resp.add_string(proto::cp_status_info, error);

	co_await send_response(client, resp);
}

/*
 * send a syserr response to the client.
 */
auto send_syserr(ctlclient &client, std::string_view syserr) -> task<void>
{
	auto resp = nvl();
	resp.add_string(proto::cp_status, proto::cv_status_error);
	resp.add_string(proto::cp_status_info, proto::ce_syserr);
	resp.add_string(proto::cp_status_syserr, syserr);

	co_await send_response(client, resp);
}

auto h_intf_list(ctlclient &client, nvl const &/*cmd*/) -> task<void> {
	auto resp = nvl();

	for (auto &&[name, intf] : iface::interfaces) {
	uint64_t	 operstate, adminstate;

		auto nvint = nvl();

		nvint.add_string(proto::cp_iface_name, intf->if_name);
		nvint.add_number(proto::cp_iface_rxrate,
				 intf->if_ibytes.get() * 8);
		nvint.add_number(proto::cp_iface_txrate,
				 intf->if_obytes.get() * 8);

		switch (intf->if_operstate) {
		case IF_OPER_NOTPRESENT:
			operstate = proto::cv_iface_oper_not_present;
			break;

		case IF_OPER_DOWN:
			operstate = proto::cv_iface_oper_down;
			break;

		case IF_OPER_LOWERLAYERDOWN:
			operstate = proto::cv_iface_oper_lower_down;
			break;

		case IF_OPER_TESTING:
			operstate = proto::cv_iface_oper_testing;
			break;

		case IF_OPER_DORMANT:
			operstate = proto::cv_iface_oper_dormant;
			break;

		case IF_OPER_UP:
			operstate = proto::cv_iface_oper_up;
			break;

		default:
			operstate = proto::cv_iface_oper_unknown;
			break;
		}
		nvint.add_number(proto::cp_iface_oper, operstate);

		if (intf->if_flags & IFF_UP)
			adminstate = proto::cv_iface_admin_up;
		else
			adminstate = proto::cv_iface_admin_down;
		nvint.add_number(proto::cp_iface_admin, adminstate);

		if (auto error = nvint.error(); error) {
			log::error("h_intf_list: nvl: {}", error->message());
			co_return;
		}

		resp.append_nvlist_array(proto::cp_iface, nvint);
	}

	if (auto error = resp.error(); error) {
		log::error("h_intf_list: resp: {}", error->message());
		co_return;
	}

	co_await send_response(client, resp);
	co_return;
}

auto h_net_list(ctlclient &client, nvl const &/*cmd*/) -> task<void> {
	auto resp = nvl();

	for (auto &&handle : network::findall()) {
		auto net = info(handle);
		if (!net)
			panic("h_net_list: network::info failed");

		auto nvnet = nvl();

		nvnet.add_string(proto::cp_net_name, net->name);
		if (auto error = nvnet.error(); error) {
			log::error("h_net_list: nvl: {}", error->message());
			co_return;
		}

		resp.append_nvlist_array(proto::cp_nets, nvnet);
	}


	if (auto error = resp.error(); error) {
		log::error("h_net_list: resp: {}", error->message());
		co_return;
	}

	co_await send_response(client, resp);
	co_return;
}

auto h_net_create(ctlclient &client, nvl const &cmd) -> task<void> {
	if (!cmd.exists_string(proto::cp_newnet_name)) {
		co_await send_error(client, proto::ce_proto);
		co_return;
	}

	auto netname = cmd.get_string(proto::cp_newnet_name);
	if (netname.size() > proto::cn_maxnetnam) {
		co_await send_error(client, proto::ce_netnmln);
		co_return;
	}

	if (auto ret = network::create(netname); !ret)
		co_await send_syserr(client, ret.error().message());

	co_await send_success(client);
	co_return;
}


auto h_net_delete(ctlclient &client, nvl const &cmd) -> task<void> {
	if (!cmd.exists_string(proto::cp_delnet_name)) {
		co_await send_error(client, proto::ce_proto);
		co_return;
	}

	auto netname = cmd.get_string(proto::cp_newnet_name);
	auto net = network::find(netname);

	if (!net) {
		co_await send_syserr(client, net.error().message());
		co_return;
	}

	network::remove(*net);
	co_await send_success(client);
	co_return;
}

} // namespace netd::ctl
