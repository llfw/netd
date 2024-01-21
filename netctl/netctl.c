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

#include	<libxo/xo.h>

#include	<assert.h>
#include	<errno.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<inttypes.h>

#include	"protocol.h"
#include	"defs.h"

int	netd_connect(void);

typedef int (*cmdhandler)(int server, int argc, char *nullable *nonnull argv);

static int	c_intf_list(int, int, char **);
static int	c_net_list(int, int, char **);
static int	c_net_create(int, int, char **);
static int	c_net_delete(int, int, char **);

typedef struct command {
	char const *nullable		cm_name;
	cmdhandler nullable		cm_handler;
	struct command *nullable	cm_subs;
	char const *nullable		cm_description;
} command_t;

static command_t intf_cmds[] = {
	{ "list",	c_intf_list, NULL,	"list interfaces" },
	{ NULL, NULL, NULL, NULL }
};

static command_t net_cmds[] = {
	{ "list",	c_net_list, NULL,	"list networks" },
	{ "create",	c_net_create, NULL,	"create new network" },
	{ "delete",	c_net_delete, NULL,	"delete existing network" },
	{ NULL, NULL, NULL, NULL },
};

static command_t root_cmds[] = {
	{ "interface",	NULL, intf_cmds,
		"configure layer 2 interfaces" },
	{ "network",	NULL, net_cmds,
		"configure layer 3 networks" },
	{ NULL, NULL, NULL, NULL }
};

static nvlist_t	*send_simple_command(int server, char const *command);

static void
usage(command_t *nonnull cmds) {
	(void)cmds;
	fprintf(stderr, "usage: %s [--libxo=...] <command>\n", getprogname());
	fprintf(stderr, "\n");
	fprintf(stderr, "commands:\n");
	fprintf(stderr, "\n");

	for (command_t *cmd = cmds; cmd->cm_name; ++cmd)
		fprintf(stderr, "  %-20s %s\n", cmd->cm_name,
			cmd->cm_description);
}

static command_t *nullable
find_command(command_t *nonnull root, int *nonnull argc,
	     char *nullable *nonnull *nonnull argv) {

	assert(root);
	assert((*argv)[0]);

	for (command_t *cmd = root; cmd->cm_name; cmd++) {
		if (strcmp(cmd->cm_name, (*argv)[0]))
			continue;

		--(*argc);
		++(*argv);

		if (cmd->cm_handler)
			return cmd;

		if (!cmd->cm_subs) {
			fprintf(stderr, "%s: unknown command\n",
				(*argv)[0]);
			usage(cmd);
			return NULL;
		}

		if (!(*argv)[0]) {
			fprintf(stderr, "incomplete command\n");
			usage(cmd->cm_subs);
			return NULL;
		}

		return find_command(cmd->cm_subs, argc, argv);
	}

	fprintf(stderr, "%s: unknown command\n", (*argv)[0]);
	usage(root);
	return NULL;
}

int
main(int argc, char **argv) {
int			server;
command_t *nullable	cmd;

	setprogname(argv[0]);

	argc = xo_parse_args(argc, argv);
	if (argc < 0)
		return EXIT_FAILURE;

	--argc;
	++argv;

	if (!argc) {
		usage(root_cmds);
		return 1;
	}

	if ((cmd = find_command(root_cmds, &argc, &argv)) == NULL)
		return 1;

	if ((server = netd_connect()) == -1)
		return 1;

	return cmd->cm_handler(server, argc, argv);
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
	assert(sizeof(CTL_SOCKET_PATH) <= sizeof(sun.sun_path));
	strcpy(sun.sun_path, CTL_SOCKET_PATH);

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

static nvlist_t *
nv_xfer(int server, nvlist_t *cmd) {
int		 err;
ssize_t		 n;
void		*cmdbuf = NULL;
size_t		 cmdsize = 0;
struct iovec	 iov;
struct msghdr	 mhdr;
char		 respbuf[CTL_MAX_MSG_SIZE];
nvlist_t	*resp = NULL;

	if ((err = nvlist_error(cmd)) != 0) {
		errno = err;
		return NULL;
	}

	if ((cmdbuf = nvlist_pack(cmd, &cmdsize)) == NULL)
		goto err;

	iov.iov_base = cmdbuf;
	iov.iov_len = cmdsize;

	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;

	n = sendmsg(server, &mhdr, MSG_EOR);
	if (n == -1)
		goto err;

	iov.iov_base = respbuf;
	iov.iov_len = sizeof(respbuf);

	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;

	n = recvmsg(server, &mhdr, 0);
	if (n == -1)
		goto err;

	if (n == 0) {
		fprintf(stderr, "%s: empty reply from server\n",
			getprogname());
		errno = EINVAL;
		goto err;
	}

	if (!(mhdr.msg_flags & MSG_EOR)) {
		errno = EINVAL;
		goto err;
	}

	resp = nvlist_unpack(respbuf, n, 0);
	if (!resp)
		goto err;

	nvlist_destroy(cmd);
	return resp;

err:
	err = errno;

	nvlist_destroy(cmd);

	if (cmdbuf)
		free(cmdbuf);

	if (resp)
		nvlist_destroy(resp);

	errno = err;
	return NULL;
}

int
c_intf_list(int server, int argc, char **argv) {
nvlist_t		*resp = NULL;
nvlist_t const *const	*intfs = NULL;
size_t			 nintfs = 0;
int			 ret = 0;

	(void)argv;

	xo_open_container("interface-list");

	if (argc) {
		xo_emit("{E/usage: %s interface list}\n",
			getprogname());
		ret = 1;
		goto done;
	}

	resp = send_simple_command(server, CC_GETIFS);
	if (!resp) {
		xo_emit("{E:/%s: failed to send command: %s\n}",
			getprogname(), strerror(errno));
		ret = 1;
		goto done;
	}

	if (!nvlist_exists_nvlist_array(resp, CP_IFACE))
		goto done;

	intfs = nvlist_get_nvlist_array(resp, CP_IFACE, &nintfs);

	xo_emit("{T:NAME/%-16s}{T:ADMIN/%-6s}{T:OPER/%-5s}"
		"{T:TX/%8s}{T:RX/%8s}\n");

	for (size_t i = 0; i < nintfs; ++i) {
	nvlist_t const *nonnull	intf = intfs[i];
	char const *nonnull	operstate = "UNK";
	char const *nonnull	adminstate = "UNK";

		if (!nvlist_exists_string(intf, CP_IFACE_NAME) ||
		    !nvlist_exists_number(intf, CP_IFACE_ADMIN) ||
		    !nvlist_exists_number(intf, CP_IFACE_OPER) ||
		    !nvlist_exists_number(intf, CP_IFACE_TXRATE) ||
		    !nvlist_exists_number(intf, CP_IFACE_RXRATE)) {
			xo_emit("{E:/%s: invalid response}\n", getprogname());
			ret = 1;
			goto done;
		}

		switch (nvlist_get_number(intf, CP_IFACE_ADMIN)) {
		case CV_IFACE_ADMIN_UP:
			adminstate = "UP";
			break;
		case CV_IFACE_ADMIN_DOWN:
			adminstate = "DOWN";
			break;
		}

		switch (nvlist_get_number(intf, CP_IFACE_OPER)) {
		case CV_IFACE_OPER_NOT_PRESENT:
			operstate = "NOHW";
			break;
		case CV_IFACE_OPER_DOWN:
			operstate = "DOWN";
			break;
		case CV_IFACE_OPER_LOWER_DOWN:
			operstate = "LDWN";
			break;
		case CV_IFACE_OPER_TESTING:
			operstate = "TEST";
			break;
		case CV_IFACE_OPER_DORMANT:
			operstate = "DRMT";
			break;
		case CV_IFACE_OPER_UP:
			operstate = "UP";
			break;
		}

		xo_open_instance("interface");
		xo_emit("{V:name/%-16s}"
			"{V:admin-state/%-6s}"
			"{V:oper-state/%-5s}"
			"{[:8}{Vhn,hn-decimal,hn-1000:txrate/%ju}b/s{]:}"
			"{[:8}{Vhn,hn-decimal,hn-1000:rxrate/%ju}b/s{]:}"
			"\n",
			nvlist_get_string(intf, CP_IFACE_NAME),
			adminstate,
			operstate,
			nvlist_get_number(intf, CP_IFACE_TXRATE),
			nvlist_get_number(intf, CP_IFACE_RXRATE));
		xo_close_instance("interface");
	}

done:
	xo_close_container("interface-list");
	xo_finish();
	nvlist_destroy(resp);
	return ret;
}

int
c_net_list(int server, int argc, char **argv) {
nvlist_t		*resp = NULL;
nvlist_t const *const	*nets = NULL;
size_t			 nnets = 0;
int			 ret = 0;

	(void)argv;

	xo_open_container("network-list");

	if (argc) {
		xo_emit("{E/usage: %s network list}\n",
			getprogname());
		ret = 1;
		goto done;
	}

	resp = send_simple_command(server, CC_GETNETS);
	if (!resp) {
		xo_emit("{E:/%s: failed to send command: %s\n}",
			getprogname(), strerror(errno));
		ret = 1;
		goto done;
	}

	if (!nvlist_exists_nvlist_array(resp, CP_NETS))
		goto done;

	nets = nvlist_get_nvlist_array(resp, CP_NETS, &nnets);

	xo_emit("{T:NAME/%-16s}\n");

	for (size_t i = 0; i < nnets; ++i) {
	nvlist_t const *nonnull	net = nets[i];

		if (!nvlist_exists_string(net, CP_NET_NAME)) {
			xo_emit("{E:/%s: invalid response}\n", getprogname());
			ret = 1;
			goto done;
		}

		xo_open_instance("network");
		xo_emit("{V:name/%-16s}\n",
			nvlist_get_string(net, CP_NET_NAME));
		xo_close_instance("network");
	}

done:
	xo_close_container("network-list");
	xo_finish();
	nvlist_destroy(resp);
	return ret;
}

int
c_net_create(int server, int argc, char **argv) {
int		 ret = 0;
nvlist_t	*cmd = NULL, *resp = NULL;
char const	*netname = NULL, *status = NULL, *error = NULL;

	(void)argv;

	if (argc != 1) {
		xo_emit("{E/usage: %s network create <name>}\n",
			getprogname());
		ret = 1;
		goto done;
	}

	netname = argv[0];

	if ((cmd = nvlist_create(0)) == NULL)  {
		xo_emit("{E:/%s: nvlist_create: %s\n}",
			getprogname(), strerror(errno));
		ret = 1;
		goto done;
	}

	nvlist_add_string(cmd, CP_CMD, CC_NEWNET);
	nvlist_add_string(cmd, CP_NEWNET_NAME, netname);

	if (nvlist_error(cmd) != 0) {
		xo_emit("{E:/%s: nvlist: %s\n}",
			getprogname(), strerror(errno));
		ret = 1;
		goto done;
	}

	resp = nv_xfer(server, cmd);
	cmd = NULL;

	if (!resp) {
		xo_emit("{E:/%s: failed to send command: %s\n}",
			getprogname(), strerror(errno));
		ret = 1;
		goto done;
	}

	if (!nvlist_exists_string(resp, CP_STATUS)) {
		xo_emit("{E:/%s: invalid response}", getprogname());
		ret = 1;
		goto done;
	}

	status = nvlist_get_string(resp, CP_STATUS);

	if (strcmp(status, CV_STATUS_SUCCESS) == 0)
		goto done;

	if (!nvlist_exists_string(resp, CP_STATUS_INFO)) {
		xo_emit("{E:/%s: invalid response}", getprogname());
		ret = 1;
		goto done;
	}

	error = nvlist_get_string(resp, CP_STATUS_INFO);
	xo_emit("{E:/%s}\n", error);

done:
	xo_finish();

	if (cmd)
		nvlist_destroy(cmd);

	if (resp)
		nvlist_destroy(resp);

	return ret;
}

int
c_net_delete(int server, int argc, char **argv) {
int		 ret = 0;
nvlist_t	*cmd = NULL, *resp = NULL;
char const	*netname = NULL, *status = NULL, *error = NULL;

	(void)argv;

	if (argc != 1) {
		xo_emit("{E/usage: %s network delete <name>}\n",
			getprogname());
		ret = 1;
		goto done;
	}

	netname = argv[0];

	if ((cmd = nvlist_create(0)) == NULL)  {
		xo_emit("{E:/%s: nvlist_create: %s\n}",
			getprogname(), strerror(errno));
		ret = 1;
		goto done;
	}

	nvlist_add_string(cmd, CP_CMD, CC_DELNET);
	nvlist_add_string(cmd, CP_DELNET_NAME, netname);

	if (nvlist_error(cmd) != 0) {
		xo_emit("{E:/%s: nvlist: %s\n}",
			getprogname(), strerror(errno));
		ret = 1;
		goto done;
	}

	resp = nv_xfer(server, cmd);
	cmd = NULL;

	if (!resp) {
		xo_emit("{E:/%s: failed to send command: %s\n}",
			getprogname(), strerror(errno));
		ret = 1;
		goto done;
	}

	if (!nvlist_exists_string(resp, CP_STATUS)) {
		xo_emit("{E:/%s: invalid response}", getprogname());
		ret = 1;
		goto done;
	}

	status = nvlist_get_string(resp, CP_STATUS);

	if (strcmp(status, CV_STATUS_SUCCESS) == 0)
		goto done;

	if (!nvlist_exists_string(resp, CP_STATUS_INFO)) {
		xo_emit("{E:/%s: invalid response}", getprogname());
		ret = 1;
		goto done;
	}

	error = nvlist_get_string(resp, CP_STATUS_INFO);
	xo_emit("{E:/%s}\n", error);

done:
	xo_finish();

	if (cmd)
		nvlist_destroy(cmd);

	if (resp)
		nvlist_destroy(resp);

	return ret;
}
static nvlist_t *
send_simple_command(int server, char const *command)
{
nvlist_t	*cmd = NULL;
int		 err;

	cmd = nvlist_create(0);
	nvlist_add_string(cmd, CP_CMD, command);
	
	if ((err = nvlist_error(cmd)) != 0) {
		errno = err;
		goto err;
	}

	return nv_xfer(server, cmd);

err:
	if (cmd)
		nvlist_destroy(cmd);

	return NULL;
}
