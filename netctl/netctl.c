/*
 * This source code is released into the public domain.
 */

#include	<sys/types.h>
#include	<sys/socket.h>
#include	<sys/un.h>
#include	<sys/nv.h>

#include	<assert.h>
#include	<errno.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>

#include	"protocol.h"

int	netd_connect(void);

int	c_list_interfaces(int, int, char **);

struct netcmd {
	char const	 *nc_name;
	int		(*nc_handler)(int, int, char **);
} netcmds[] = {
	{ "list-interfaces",	c_list_interfaces },
};

static nvlist_t	*send_simple_command(int server, char const *command);

static void
usage(void) {
	fprintf(stderr, "usage: %s <command>\n", getprogname());
}

int
main(int argc, char **argv) {
int	server;

	setprogname(argv[0]);

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

	i = connect(sock, (struct sockaddr *)&sun, SUN_LEN(&sun));
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

	(void)argv;

	if (argc) {
		fprintf(stderr, "usage: %s list-interfaces\n",
			getprogname());
		return 1;
	}

	resp = send_simple_command(server, CTL_CMD_LIST_INTERFACES);
	if (!resp) {
		fprintf(stderr, "%s: failed to send command: %s\n",
			getprogname(), strerror(errno));
		return 1;
	}

	if (!nvlist_exists_nvlist_array(resp, CTL_PARM_INTERFACES)) {
		fprintf(stderr, "%s: no interfaces configured\n",
			getprogname());
		nvlist_destroy(resp);
		return 0;
	}

	intfs = nvlist_get_nvlist_array(resp, CTL_PARM_INTERFACES, &nintfs);

	printf("NAME\n");

	for (size_t i = 0; i < nintfs; ++i) {
	nvlist_t const	*intf = intfs[i];

		if (!nvlist_exists_string(intf, CTL_PARM_INTERFACE_NAME)) {
			fprintf(stderr, "%s: invalid response\n",
				getprogname());
			goto err;
		}

		printf("%s\n",
		       nvlist_get_string(intf, CTL_PARM_INTERFACE_NAME));
	}

	nvlist_destroy(resp);
	return 0;

err:
	nvlist_destroy(resp);
	return 1;
}

static nvlist_t *
nv_xfer(int server, nvlist_t *cmd) {
int		 err;
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

	err = sendmsg(server, &mhdr, MSG_EOR);
	if (err == -1)
		goto err;

	iov.iov_base = respbuf;
	iov.iov_len = sizeof(respbuf);

	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;

	err = recvmsg(server, &mhdr, 0);
	if (err == -1)
		goto err;

	if (err == 0) {
		fprintf(stderr, "%s: empty reply from server\n",
			getprogname());
		errno = EINVAL;
		goto err;
	}

	if (!(mhdr.msg_flags & MSG_EOR)) {
		errno = EINVAL;
		goto err;
	}

	resp = nvlist_unpack(respbuf, err, 0);
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
