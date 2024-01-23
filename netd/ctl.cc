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

#include	<netlink/netlink.h>
#include	<netlink/route/interface.h>

#include	<net/if.h>

#include	<cassert>
#include	<cerrno>
#include	<cstdlib>
#include	<cstring>
#include	<unistd.h>

#include	<new>
#include	<print>

#include	"netd.hh"
#include	"ctl.hh"
#include	"log.hh"
#include	"kq.hh"
#include	"protocol.hh"
#include	"iface.hh"
#include	"network.hh"
#include	"nvl.hh"

typedef struct ctlclient {
	int	cc_fd = 0;
	std::array<std::byte, proto::max_msg_size>
		cc_buf = {};
} ctlclient_t;

using cmdhandler = std::function<void (ctlclient_t *, nvl const &)>;

struct chandler {
	std::string_view	ch_cmd;
	cmdhandler		ch_handler;
};

auto send_error(ctlclient_t *nonnull, std::string_view) -> void;
auto send_success(ctlclient_t *nonnull, std::string_view = {}) -> void;
auto send_syserr(ctlclient_t *nonnull, std::string_view) -> void;

/*
 * command handlers
 */

static void h_intf_list(ctlclient_t *, nvl const &);
static void h_net_create(ctlclient_t *, nvl const &);
static void h_net_delete(ctlclient_t *, nvl const &);
static void h_net_list(ctlclient_t *, nvl const &);

static std::vector<chandler> chandlers{
	{ proto::cc_getifs,	h_intf_list	},
	{ proto::cc_getnets,	h_net_list	},
	{ proto::cc_newnet,	h_net_create	},
	{ proto::cc_delnet,	h_net_delete	},
};

static kq::disp	readclient	(int fd, ssize_t, int, ctlclient *);
static kq::disp	acceptclient	(int, int, struct sockaddr * nullable,
				 socklen_t);
static void	clientcmd	(ctlclient_t *nonnull client, nvl const &cmd);

int
ctl_setup(void) {
struct sockaddr_un	sun;
int			sock = -1;
std::string		path(proto::socket_path); // for unlink

	(void) unlink(path.c_str());

	if ((sock = kq::socket(AF_UNIX,
			       SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
			       0)) == -1) {
		nlog::fatal("ctl_setup: socket: {}", strerror(errno));
		goto err;
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	assert(proto::socket_path.size() < sizeof(sun.sun_path));
	std::ranges::copy(proto::socket_path, &sun.sun_path[0]);

	if (bind(sock,
		 (struct sockaddr *)&sun,
		 (socklen_t)SUN_LEN(&sun)) == -1) {
		nlog::fatal("ctl_setup: bind: {}", strerror(errno));
		goto err;
	}

	if (listen(sock, 128) == -1) {
		nlog::fatal("ctl_setup: listen: {}", strerror(errno));
		goto err;
	}

	if (kq::accept4(sock,
			SOCK_CLOEXEC | SOCK_NONBLOCK,
			acceptclient) == -1) {
		nlog::fatal("ctl_setup: kqaccept4: {}",
		     strerror(errno));
		goto err;
	}

	nlog::debug("ctl_setup: listening on {}", path);
	return 0;

err:
	if (sock != -1)
		kq::close(sock);
	return -1;
}

static kq::disp
acceptclient(int lsnfd, int fd, struct sockaddr *addr, socklen_t addrlen) {
ctlclient_t	*client = NULL;

	(void)lsnfd;
	(void)addr;
	(void)addrlen;

	if (fd == -1)
		panic("acceptclient: accept failed: %s", strerror(errno));

	if ((client = new (std::nothrow) ctlclient) == NULL)
		goto err;

	client->cc_fd = fd;

	{
		auto handler = [=] (int fd, ssize_t nbytes, int flags) {
			return readclient(fd, nbytes, flags, client);
		};
		kq::recvmsg(client->cc_fd,
			    std::span(client->cc_buf),
			    handler);
	}

	nlog::debug("acceptclient: new client fd={}", client->cc_fd);
	return kq::disp::rearm;

err:
	if (client) {
		if (client->cc_fd != -1)
			kq::close(client->cc_fd);
		delete client;
	}
	return kq::disp::rearm;
}

/*
 * read a command from the given client and execute it.
 */
static kq::disp
readclient(int, ssize_t nbytes, int flags, ctlclient *client) {
	// TODO: close the client on error

	if (nbytes == 0)
		// client disconnected
		return kq::disp::stop;

	if (nbytes < 0) {
		nlog::debug("readclient: kqrecvmsg: {}", strerror(errno));
		return kq::disp::stop;
	}

	assert(client);
	assert(flags & MSG_EOR);

	nlog::debug("readclient: fd={}", client->cc_fd);

	auto bytes = std::span(client->cc_buf).subspan(0, std::size_t(nbytes));
	auto cmd = nvl::unpack(bytes, 0);
	if (!cmd) {
		nlog::debug("readclient: nvl::unpack: {}",
		     cmd.error().message());
		return kq::disp::stop;
	}

	// TODO: is this check necessary?
	if (auto error = cmd->error(); error) {
		nlog::debug("readclient: nvlist error: {}", error->message());
		return kq::disp::stop;
	}

	clientcmd(client, *cmd);

	kq::close(client->cc_fd);
	delete client;
	return kq::disp::stop;
}

/*
 * handle a command from a client and reply to it.
 */
static void
clientcmd(ctlclient_t *client, nvl const &cmd)
{
	if (!cmd.exists_string(proto::cp_cmd)) {
		nlog::debug("clientcmd: missing cp_cmd");
		return;
	}

	auto cmdname = cmd.get_string(proto::cp_cmd);
	nlog::debug("clientcmd: cmd={}", std::string(cmdname));

	for (auto handler : chandlers) {
		if (cmdname != handler.ch_cmd)
			continue;

		handler.ch_handler(client, cmd);
		return;
	}

	/* TODO: unknown command, send an error */
}

/*
 * send the given response to the client.
 */
static void
send_response(ctlclient_t *client, nvl const &resp) {
struct msghdr	 mhdr;
struct iovec	 iov;
ssize_t		 n;

	if (auto error = resp.error(); error) {
		nlog::debug("send_response: nvlist error: {}",
		     error->message());
		return;
	}

	auto rbuf = resp.pack();
	if (!rbuf) {
		nlog::debug("send_response: nvlist_pack failed: {}",
		     rbuf.error().message());
		return;
	}

	iov.iov_base = rbuf->data();
	iov.iov_len = rbuf->size();

	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;

	/* TODO: assume this won't block for now */
	n = ::sendmsg(client->cc_fd, &mhdr, MSG_EOR);
	if (n == -1)
		nlog::debug("send_response: sendmsg: {}", strerror(errno));
}

/*
 * send a success response to the client, with optional STATUS_INFO.
 */
auto send_success(ctlclient_t *nonnull client, std::string_view info) -> void
{
	assert(client);

	auto resp = nvl();

	resp.add_string(proto::cp_status, proto::cv_status_success);
	if (!info.empty())
		resp.add_string(proto::cp_status_info, info);

	if (auto error = resp.error(); error) {
		nlog::error("send_success: nvl pack error: {}",
		     error->message());
		return;
	}

	send_response(client, resp);
}

/*
 * send an error response to the client.
 */
auto send_error(ctlclient_t *nonnull client, std::string_view error) -> void {
	assert(client);

	auto resp = nvl();
	resp.add_string(proto::cp_status, proto::cv_status_error);
	resp.add_string(proto::cp_status_info, error);

	send_response(client, resp);
}

/*
 * send a syserr response to the client.
 */
auto send_syserr(ctlclient_t *nonnull client, std::string_view syserr)
	-> void
{
	assert(client);

	auto resp = nvl();
	resp.add_string(proto::cp_status, proto::cv_status_error);
	resp.add_string(proto::cp_status_info, proto::ce_syserr);
	resp.add_string(proto::cp_status_syserr, syserr);

	send_response(client, resp);
}

auto h_intf_list(ctlclient_t *client, nvl const &/*cmd*/) -> void {
	auto resp = nvl();

	for (auto &&[name, intf] : interfaces) {
	uint64_t	 operstate, adminstate;

		auto nvint = nvl();

		nvint.add_string(proto::cp_iface_name, intf->if_name);
		nvint.add_number(proto::cp_iface_rxrate, intf->if_rxrate);
		nvint.add_number(proto::cp_iface_txrate, intf->if_txrate);

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
			nlog::error("h_intf_list: nvl: {}", error->message());
			return;
		}

		resp.append_nvlist_array(proto::cp_iface, nvint);
	}

	if (auto error = resp.error(); error) {
		nlog::error("h_intf_list: resp: {}", error->message());
		return;
	}

	send_response(client, resp);
}

static void
h_net_list(ctlclient_t *client, nvl const &/*cmd*/) {
	auto resp = nvl();

	for (auto &&[name, net] : networks) {
		auto nvnet = nvl();

		nvnet.add_string(proto::cp_net_name, net->name());
		if (auto error = nvnet.error(); error) {
			nlog::error("h_net_list: nvl: {}", error->message());
			return;
		}

		resp.append_nvlist_array(proto::cp_nets, nvnet);
	}


	if (auto error = resp.error(); error) {
		nlog::error("h_net_list: resp: {}", error->message());
		return;
	}

	send_response(client, resp);
}

auto h_net_create(ctlclient_t *client, nvl const &cmd) -> void {
	if (!cmd.exists_string(proto::cp_newnet_name)) {
		send_error(client, proto::ce_proto);
		return;
	}

	auto netname = cmd.get_string(proto::cp_newnet_name);
	if (netname.size() > proto::cn_maxnetnam) {
		send_error(client, proto::ce_netnmln);
		return;
	}

	if (netcreate(netname) == NULL)
		send_syserr(client, strerror(errno));

	send_success(client);
}


auto h_net_delete(ctlclient_t *client, nvl const &cmd) -> void {
	if (!cmd.exists_string(proto::cp_delnet_name)) {
		send_error(client, proto::ce_proto);
		return;
	}

	auto netname = cmd.get_string(proto::cp_newnet_name);
	auto *net = netfind(netname);

	if (net == nullptr) {
		send_error(client, proto::ce_netnx);
		return;
	}

	netdelete(net);
	send_success(client);
}
