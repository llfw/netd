/*
 * This source code is released into the public domain.
 */

#include	<sys/types.h>
#include	<sys/socket.h>
#include	<sys/un.h>
#include	<sys/nv.h>

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
#include	"state.h"

typedef struct ctlclient {
	int	cc_fd;
	char	cc_buf[CTL_MAX_MSG_SIZE];
} ctlclient_t;

typedef struct chandler {
	char const	*ch_cmd;
	void		(*ch_handler)(struct kq *, ctlclient_t *client,
				      nvlist_t *cmd);
} chandler_t;

static void h_list_interfaces(struct kq *, ctlclient_t *, nvlist_t *);

static chandler_t chandlers[] = {
	{ CTL_CMD_LIST_INTERFACES, h_list_interfaces },
};

static kq_disposition_t	newclient(struct kq *, int fd, void *udata);
static kq_disposition_t	readclient(struct kq *, int fd, void *udata);
static int		acceptclient(struct kq *, int fd);
static void		clientcmd(struct kq *, ctlclient_t *, nvlist_t *cmd);

int
ctl_setup(struct kq *kq) {
struct sockaddr_un	sun;
int			sock = -1;

	(void) unlink(CTL_SOCKET_PATH);

	if ((sock = socket(AF_UNIX,
			   SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
			   0)) == -1) {
		nlog(NLOG_FATAL, "ctl_setup: socket: %s", strerror(errno));
		goto err;
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	assert(sizeof(CTL_SOCKET_PATH) <= sizeof(sun.sun_path));
	strcpy(sun.sun_path, CTL_SOCKET_PATH);

	if (bind(sock, (struct sockaddr *)&sun, SUN_LEN(&sun)) == -1) {
		nlog(NLOG_FATAL, "ctl_setup: bind: %s", strerror(errno));
		goto err;
	}

	if (listen(sock, 128) == -1) {
		nlog(NLOG_FATAL, "ctl_setup: listen: %s", strerror(errno));
		goto err;
	}

	if (kq_register_read(kq, sock, newclient, NULL) == -1) {
		nlog(NLOG_FATAL, "ctl_setup: kq_register_read: %s",
		     strerror(errno));
		goto err;
	}

	nlog(NLOG_DEBUG, "ctl_setup: listening on %s", CTL_SOCKET_PATH);
	return 0;

err:
	if (sock != -1)
		close(sock);

	return -1;
}

static kq_disposition_t
newclient(struct kq *kq, int fd, void *udata) {
	(void) udata;

	while (acceptclient(kq, fd) != -1)
		;

	if (errno == EAGAIN)
		return KQ_REARM;

	panic("ctl newclient: acceptclient() failed: %s", strerror(errno));
}

static int
acceptclient(struct kq *kq, int fd) {
int		 cfd = -1;
ctlclient_t	*client = NULL;

	cfd = accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (cfd == -1)
		goto err;

	client = calloc(1, sizeof(*client));
	if (client == NULL)
		goto err;

	client->cc_fd = cfd;
	if (kq_register_read(kq, client->cc_fd, readclient, client) == -1)
		goto err;

	nlog(NLOG_DEBUG, "acceptclient: new client fd=%d", cfd);
	return 0;

err:
	if (cfd != -1)
		close(cfd);
	if (client)
		free(client);
	return -1;
}

/*
 * read a command from the given client and execute it.
 */
static kq_disposition_t
readclient(struct kq *kq, int fd, void *udata) {
ctlclient_t	*client = udata;
ssize_t		 i;
struct msghdr	 mhdr;
struct iovec	 iov;
nvlist_t	*cmd = NULL;

	(void)fd;
	(void)kq;

	nlog(NLOG_DEBUG, "readclient: fd=%d", client->cc_fd);

	iov.iov_base = client->cc_buf;
	iov.iov_len = sizeof(client->cc_buf);

	memset(&mhdr, 0, sizeof(mhdr));
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 1;

	i = recvmsg(client->cc_fd, &mhdr, 0);
	if (i == -1) {
		nlog(NLOG_DEBUG, "readclient: recvmsg: %s", strerror(errno));
		goto err;
	}

	if (!(mhdr.msg_flags & MSG_EOR)) {
		nlog(NLOG_DEBUG, "readclient: msg too long (read %d)", i);
		goto err;
	}

	nlog(NLOG_DEBUG, "readclient: got msg");

	cmd = nvlist_unpack(client->cc_buf, i, 0);
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

	clientcmd(kq, client, cmd);

	/* fallthrough - we handled the command so close the client */
err:
	if (cmd)
		nvlist_destroy(cmd);

	close(client->cc_fd);
	free(client);
	return KQ_STOP;
}

/*
 * handle a command from a client and reply to it.
 */
static void
clientcmd(struct kq *kq, ctlclient_t *client, nvlist_t *cmd)
{
char const	*cmdname;

	(void)client;

	if (!nvlist_exists_string(cmd, CTL_CMD_NAME)) {
		nlog(NLOG_DEBUG, "clientcmd: missing CTL_CMD_NAME");
		return;
	}

	cmdname = nvlist_get_string(cmd, CTL_CMD_NAME);
	nlog(NLOG_DEBUG, "clientcmd: cmd=%s", cmdname);

	for (size_t i = 0; i < sizeof(chandlers) / sizeof(*chandlers); ++i) {
		if (strcmp(cmdname, chandlers[i].ch_cmd))
			continue;

		chandlers[i].ch_handler(kq, client, cmd);
		return;
	}

	/* TODO: unknown command, send an error */
}

static void
send_response(struct kq *kq, ctlclient_t *client, nvlist_t *resp) {
char		*rbuf = NULL;
size_t		 rsz = 0;
struct msghdr	 mhdr;
struct iovec	 iov;
ssize_t		 n;
int		 i;

	(void)kq;

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

static void
h_list_interfaces(struct kq *kq, ctlclient_t *client, nvlist_t *cmd) {
size_t		  nintfs = 0, n = 0;
interface_t	 *intf = NULL;
nvlist_t const	**nvintfs = NULL;
nvlist_t	 *resp = NULL;
int		  i;

	(void)kq;
	(void)client;
	(void)cmd;

	if ((resp = nvlist_create(0)) == NULL)
		goto err;

	/* work out how many interfaces we need to allocate */
	SLIST_FOREACH(intf, &interfaces, if_entry)
		++nintfs;

	nlog(NLOG_DEBUG, "h_list_interfaces: nintfs=%u",
	     (unsigned) nintfs);

	if (nintfs) {
		nvintfs = calloc(nintfs, sizeof(nvlist_t *));

		/* add each interface to the response */
		SLIST_FOREACH(intf, &interfaces, if_entry) {
		nvlist_t	*nvl = nvlist_create(0);

			nvlist_add_string(nvl,
					  CTL_PARM_INTERFACE_NAME,
					  intf->if_name);

			if ((i = nvlist_error(nvl)) != 0) {
				nlog(NLOG_DEBUG, "h_list_interfaces: nvl: ",
				     strerror(i));
				goto err;
			}

			nvintfs[n] = nvl;
			++n;
		}

		nvlist_add_nvlist_array(resp, CTL_PARM_INTERFACES,
					nvintfs, nintfs);
	}

	if ((i = nvlist_error(resp)) != 0) {
		nlog(NLOG_DEBUG, "h_list_interfaces: resp: %s", strerror(i));
		goto err;
	}

	send_response(kq, client, resp);

err:
	if (resp)
		nvlist_destroy(resp);
}
