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

#include	<assert.h>
#include	<errno.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>

#include	"netd.h"
#include	"ctl.h"
#include	"log.h"
#include	"kq.h"
#include	"protocol.h"
#include	"iface.h"
#include	"network.h"

typedef struct ctlclient {
	int	cc_fd;
	char	cc_buf[CTL_MAX_MSG_SIZE];
} ctlclient_t;

typedef struct chandler {
	char const	*ch_cmd;
	void		(*ch_handler)(ctlclient_t *client, nvlist_t *cmd);
} chandler_t;

/*
 * command handlers
 */

static void h_intf_list(ctlclient_t *, nvlist_t *);
static void h_net_create(ctlclient_t *, nvlist_t *);
static void h_net_delete(ctlclient_t *, nvlist_t *);
static void h_net_list(ctlclient_t *, nvlist_t *);

static chandler_t chandlers[] = {
	{ CC_GETIFS,	h_intf_list	},
	{ CC_GETNETS,	h_net_list	},
	{ CC_NEWNET,	h_net_create	},
	{ CC_DELNET,	h_net_delete	},
};

static kqdisp	readclient	(int fd, ssize_t, int,
				 void *nullable udata);
static kqdisp	acceptclient	(int, int, struct sockaddr * nullable,
				 socklen_t, void *nullable);
static void	clientcmd	(ctlclient_t *nonnull client,
				 nvlist_t *nonnull cmd);

int
ctl_setup(void) {
struct sockaddr_un	sun;
int			sock = -1;

	(void) unlink(CTL_SOCKET_PATH);

	if ((sock = kqsocket(AF_UNIX,
			     SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
			     0)) == -1) {
		nlog(NLOG_FATAL, "ctl_setup: socket: %s", strerror(errno));
		goto err;
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	assert(sizeof(CTL_SOCKET_PATH) <= sizeof(sun.sun_path));
	strcpy(sun.sun_path, CTL_SOCKET_PATH);

	if (bind(sock,
		 (struct sockaddr *)&sun,
		 (socklen_t)SUN_LEN(&sun)) == -1) {
		nlog(NLOG_FATAL, "ctl_setup: bind: %s", strerror(errno));
		goto err;
	}

	if (listen(sock, 128) == -1) {
		nlog(NLOG_FATAL, "ctl_setup: listen: %s", strerror(errno));
		goto err;
	}

	if (kqaccept4(sock, SOCK_CLOEXEC | SOCK_NONBLOCK,
		      acceptclient, NULL) == -1) {
		nlog(NLOG_FATAL, "ctl_setup: kqaccept4: %s",
		     strerror(errno));
		goto err;
	}

	nlog(NLOG_DEBUG, "ctl_setup: listening on %s", CTL_SOCKET_PATH);
	return 0;

err:
	if (sock != -1)
		kqclose(sock);
	return -1;
}

static kqdisp
acceptclient(int lsnfd, int fd, struct sockaddr *addr, socklen_t addrlen,
	     void *udata) {
ctlclient_t	*client = NULL;

	(void)lsnfd;
	(void)addr;
	(void)addrlen;
	(void)udata;

	if (fd == -1)
		panic("acceptclient: accept failed: %s", strerror(errno));

	if ((client = calloc(1, sizeof(*client))) == NULL)
		goto err;

	client->cc_fd = fd;

	kqrecvmsg(client->cc_fd, client->cc_buf, sizeof(client->cc_buf),
		  readclient, client);

	nlog(NLOG_DEBUG, "acceptclient: new client fd=%d", client->cc_fd);
	return KQ_REARM;

err:
	if (client) {
		if (client->cc_fd != -1)
			kqclose(client->cc_fd);
		free(client);
	}
	return KQ_REARM;
}

/*
 * read a command from the given client and execute it.
 */
static kqdisp
readclient(int fd, ssize_t nbytes, int flags, void *udata) {
ctlclient_t	*client = udata;
int		 i;
nvlist_t	*cmd = NULL;

	(void)fd;
	assert(flags & MSG_EOR);
	assert(udata);

	nlog(NLOG_DEBUG, "readclient: fd=%d", client->cc_fd);

	cmd = nvlist_unpack(client->cc_buf, nbytes, 0);
	if (cmd == NULL) {
		nlog(NLOG_DEBUG, "readclient: nvlist_unpack: %s",
		     strerror(errno));
		goto err;
	}

	if ((i = nvlist_error(cmd)) != 0) {
		nlog(NLOG_DEBUG, "readclient: nvlist error: %s",
		     strerror(i));
		goto err;
	}

	clientcmd(client, cmd);

	/* fallthrough - we handled the command so close the client */
err:
	if (cmd)
		nvlist_destroy(cmd);

	kqclose(client->cc_fd);
	free(client);
	return KQ_STOP;
}

/*
 * handle a command from a client and reply to it.
 */
static void
clientcmd(ctlclient_t *client, nvlist_t *cmd)
{
char const	*cmdname;

	(void)client;

	if (!nvlist_exists_string(cmd, CP_CMD)) {
		nlog(NLOG_DEBUG, "clientcmd: missing CTL_CMD_NAME");
		return;
	}

	cmdname = nvlist_get_string(cmd, CP_CMD);
	nlog(NLOG_DEBUG, "clientcmd: cmd=%s", cmdname);

	for (size_t i = 0; i < sizeof(chandlers) / sizeof(*chandlers); ++i) {
		if (strcmp(cmdname, chandlers[i].ch_cmd))
			continue;

		chandlers[i].ch_handler(client, cmd);
		return;
	}

	/* TODO: unknown command, send an error */
}

/*
 * send the given response to the client.  the response will be freed.
 */
static void
send_response(ctlclient_t *client, nvlist_t *resp) {
char		*rbuf = NULL;
size_t		 rsz = 0;
struct msghdr	 mhdr;
struct iovec	 iov;
ssize_t		 n;
int		 i;

	if ((i = nvlist_error(resp)) != 0) {
		nlog(NLOG_DEBUG, "send_response: nvlist error: %s",
		     strerror(i));
		return;
	}

	rbuf = nvlist_pack(resp, &rsz);
	if (rbuf == NULL) {
		nlog(NLOG_DEBUG, "send_response: nvlist_pack failed: %s",
		     strerror(errno));
		return;
	}

	iov.iov_base = rbuf;
	iov.iov_len = rsz;

	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;

	/* TODO: assume this won't block for now */
	n = sendmsg(client->cc_fd, &mhdr, MSG_EOR);
	if (n == -1)
		nlog(NLOG_DEBUG, "send_response: sendmsg: %s",
		     strerror(errno));
}

/*
 * send a success response to the client, with optional STATUS_INFO.
 */
static void
send_success(ctlclient_t *nonnull client, char const *nullable info) {
nvlist_t	*resp = NULL;

	assert(client);

	if ((resp = nvlist_create(0)) == NULL)
		goto err;

	nvlist_add_string(resp, CP_STATUS, CV_STATUS_SUCCESS);
	if (info)
		nvlist_add_string(resp, CP_STATUS_INFO, info);

	send_response(client, resp);

err:
	if (resp)
		nvlist_destroy(resp);
}

/*
 * send an error response to the client.
 */
static void
send_error(ctlclient_t *nonnull client, char const *nonnull error) {
nvlist_t	*resp = NULL;

	assert(client);
	assert(error);

	if ((resp = nvlist_create(0)) == NULL)
		goto err;

	nvlist_add_string(resp, CP_STATUS, CV_STATUS_ERROR);
	nvlist_add_string(resp, CP_STATUS_INFO, error);

	send_response(client, resp);

err:
	if (resp)
		nvlist_destroy(resp);
}

/*
 * send a syserr response to the client.
 */
static void
send_syserr(ctlclient_t *nonnull client, char const *nonnull syserr) {
nvlist_t	*resp = NULL;

	assert(client);
	assert(syserr);

	if ((resp = nvlist_create(0)) == NULL)
		goto err;

	nvlist_add_string(resp, CP_STATUS, CV_STATUS_ERROR);
	nvlist_add_string(resp, CP_STATUS_INFO, CE_SYSERR);
	nvlist_add_string(resp, CP_STATUS_SYSERR, syserr);

	send_response(client, resp);

err:
	if (resp)
		nvlist_destroy(resp);
}

static void
h_intf_list(ctlclient_t *client, nvlist_t *cmd) {
size_t		  nintfs = 0, n = 0;
interface_t	 *intf = NULL;
nvlist_t const	**nvintfs = NULL;
nvlist_t	 *resp = NULL;
int		  i;

	(void)client;
	(void)cmd;

	if ((resp = nvlist_create(0)) == NULL)
		goto err;

	/* work out how many interfaces we need to allocate */
	for (intf = interfaces; intf; intf = intf->if_next)
		++nintfs;

	nlog(NLOG_DEBUG, "h_intf_list: nintfs=%u",
	     (unsigned) nintfs);

	if (nintfs) {
		nvintfs = calloc(nintfs, sizeof(nvlist_t *));

		/* add each interface to the response */
		for (intf = interfaces; intf; intf = intf->if_next) {
		nvlist_t	*nvl = nvlist_create(0);
		uint64_t	 operstate, adminstate;

			nvlist_add_string(nvl, CP_IFACE_NAME, intf->if_name);

			nvlist_add_number(nvl, CP_IFACE_RXRATE,
					  intf->if_rxrate);
			nvlist_add_number(nvl, CP_IFACE_TXRATE,
					  intf->if_txrate);

			switch (intf->if_operstate) {
			case IF_OPER_NOTPRESENT:
				operstate = CV_IFACE_OPER_NOT_PRESENT;
				break;

			case IF_OPER_DOWN:
				operstate = CV_IFACE_OPER_DOWN;
				break;

			case IF_OPER_LOWERLAYERDOWN:
				operstate = CV_IFACE_OPER_LOWER_DOWN;
				break;

			case IF_OPER_TESTING:
				operstate = CV_IFACE_OPER_TESTING;
				break;

			case IF_OPER_DORMANT:
				operstate = CV_IFACE_OPER_DORMANT;
				break;

			case IF_OPER_UP:
				operstate = CV_IFACE_OPER_UP;
				break;

			default:
				operstate = CV_IFACE_OPER_UNKNOWN;
				break;
			}
			nvlist_add_number(nvl, CP_IFACE_OPER, operstate);

			if (intf->if_flags & IFF_UP)
				adminstate = CV_IFACE_ADMIN_UP;
			else
				adminstate = CV_IFACE_ADMIN_DOWN;
			nvlist_add_number(nvl, CP_IFACE_ADMIN, adminstate);

			if ((i = nvlist_error(nvl)) != 0) {
				nlog(NLOG_DEBUG, "h_intf_list: nvl: %s",
				     strerror(i));
				goto err;
			}

			nvintfs[n] = nvl;
			++n;
		}

		nvlist_add_nvlist_array(resp, CP_IFACE,
					nvintfs, nintfs);
	}

	if ((i = nvlist_error(resp)) != 0) {
		nlog(NLOG_DEBUG, "h_intf_list: resp: %s", strerror(i));
		goto err;
	}

	send_response(client, resp);

err:
	if (nvintfs)
		free(nvintfs);

	if (resp)
		nvlist_destroy(resp);
}

static void
h_net_list(ctlclient_t *client, nvlist_t *cmd) {
network_t	 *net = NULL;
nvlist_t	 *resp = NULL;
int		  i;

	(void)cmd;

	if ((resp = nvlist_create(0)) == NULL)
		goto err;

	for (net = networks; net; net = net->net_next) {
	nvlist_t	*nvnet = nvlist_create(0);

		nvlist_add_string(nvnet, CP_NET_NAME, net->net_name);

		if ((i = nvlist_error(nvnet)) != 0) {
			nlog(NLOG_DEBUG, "h_net_list: nvl: %s",
			     strerror(i));
			goto err;
		}

		nvlist_append_nvlist_array(resp, CP_NETS, nvnet);
	}


	if ((i = nvlist_error(resp)) != 0) {
		nlog(NLOG_DEBUG, "h_net_list: resp: %s", strerror(i));
		goto err;
	}

	send_response(client, resp);

err:
	if (resp)
		nvlist_destroy(resp);
}

static void
h_net_create(ctlclient_t *client, nvlist_t *cmd) {
char const	*netname = NULL;

	if (!nvlist_exists_string(cmd, CP_NEWNET_NAME)) {
		send_error(client, CE_PROTO);
		return;
	}

	netname = nvlist_get_string(cmd, CP_NEWNET_NAME);
	if (strlen(netname) > CN_MAXNETNAM) {
		send_error(client, CE_NETNMLN);
		return;
	}

	if (netcreate(netname) == NULL)
		send_syserr(client, strerror(errno));
	else
		send_success(client, NULL);
}


static void
h_net_delete(ctlclient_t *client, nvlist_t *cmd) {
char const	*netname = NULL;
network_t	*net = NULL;

	if (!nvlist_exists_string(cmd, CP_DELNET_NAME)) {
		send_error(client, CE_PROTO);
		return;
	}

	netname = nvlist_get_string(cmd, CP_NEWNET_NAME);

	if ((net = netfind(netname)) == NULL) {
		send_error(client, CE_NETNX);
		return;
	}

	netdelete(net);
	send_success(client, NULL);
}
