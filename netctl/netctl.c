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

int	netd_connect(void);

int	c_list_interfaces(int, int, char **);

static struct netcmd {
	char const	 *nc_name;
	int		(*nc_handler)(int, int, char **);
} netcmds[] = {
	{ "list-interfaces",	c_list_interfaces },
};

static nvlist_t	*send_simple_command(int server, char const *command);

static void
usage(void) {
	fprintf(stderr, "usage: %s [--libxo=...] <command>\n", getprogname());
}

int
main(int argc, char **argv) {
int	server;

	setprogname(argv[0]);

	argc = xo_parse_args(argc, argv);
	if (argc < 0)
		return EXIT_FAILURE;

	--argc;
	++argv;

	if (!argc) {
		usage();
		return 1;
	}

	for (size_t i = 0; i < sizeof(netcmds) / sizeof(*netcmds); ++i) {
		if (strcmp(argv[0], netcmds[i].nc_name))
			continue;

		--argc;
		++argv;

		if ((server = netd_connect()) == -1)
			return 1;

		return netcmds[i].nc_handler(server, argc, argv);
	}

	fprintf(stderr, "%s: unrecognised command: %s\n",
		getprogname(), argv[0]);

	return 0;
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

int
c_list_interfaces(int server, int argc, char **argv) {
nvlist_t		*resp = NULL;
nvlist_t const *const	*intfs = NULL;
size_t			 nintfs = 0;
int			 ret = 0;

	(void)argv;

	xo_open_container("interface-list");

	if (argc) {
		xo_emit("{E/usage: %s list-interfaces}\n",
			getprogname());
		ret = 1;
		goto done;
	}

	resp = send_simple_command(server, CTL_CMD_LIST_INTERFACES);
	if (!resp) {
		xo_emit("{E:/%s: failed to send command: %s\n}",
			getprogname(), strerror(errno));
		ret = 1;
		goto done;
	}

	if (!nvlist_exists_nvlist_array(resp, CTL_PARM_INTERFACES))
		goto done;

	intfs = nvlist_get_nvlist_array(resp, CTL_PARM_INTERFACES, &nintfs);

	xo_emit("{T:NAME/%-16s}{T:TX/%8s}{T:RX/%8s}\n");

	for (size_t i = 0; i < nintfs; ++i) {
	nvlist_t const	*intf = intfs[i];

		if (!nvlist_exists_string(intf, CTL_PARM_INTERFACE_NAME) ||
		    !nvlist_exists_number(intf, CTL_PARM_INTERFACE_TXRATE) ||
		    !nvlist_exists_number(intf, CTL_PARM_INTERFACE_RXRATE)) {
			xo_emit("{E:/%s: invalid response}\n", getprogname());
			ret = 1;
			goto done;
		}

		xo_open_instance("interface");
		xo_emit("{V:name/%-16s}"
			"{[:8}{Vhn,hn-decimal,hn-1000:txrate/%ju}b/s{]:}"
			"{[:8}{Vhn,hn-decimal,hn-1000:rxrate/%ju}b/s{]:}"
			"\n",
			nvlist_get_string(intf, CTL_PARM_INTERFACE_NAME),
			nvlist_get_number(intf, CTL_PARM_INTERFACE_TXRATE),
			nvlist_get_number(intf, CTL_PARM_INTERFACE_RXRATE));
		xo_close_instance("interface");
	}

done:
	xo_close_container("interface-list");
	xo_finish();
	nvlist_destroy(resp);
	return ret;
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

static nvlist_t *
send_simple_command(int server, char const *command)
{
nvlist_t	*cmd = NULL;
int		 err;

	cmd = nvlist_create(0);
	nvlist_add_string(cmd, CTL_CMD_NAME, command);
	
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
