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
#include	<sys/event.h>
#include	<sys/socket.h>

#include	<assert.h>
#include	<errno.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdio.h>
#include	<time.h>
#include	<unistd.h>

#include	"netd.h"
#include	"kq.h"
#include	"log.h"

time_t current_time;

/* kq_fd represents a single open fd */
#define	KQF_OPEN	0x1u
#define	KFD_IS_OPEN(kfd)	(((kfd)->kf_flags & KQF_OPEN) == KQF_OPEN)

typedef struct kq_fd {
	kqonreadcb	 kf_readh;
	void		*kf_udata;
	uint8_t		 kf_flags;
} kq_fd_t;

/* kq represents a kqueue instance */
typedef struct kq {
	int	 kq_fd;
	kq_fd_t	*kq_fdtable;
	size_t	 kq_nfds;
} kq_t;

static kq_fd_t	*kq_get_fd(int fd);

/* the global instance */
static kq_t kq;

int
kqopen(int fd) {
size_t	elem = (size_t)fd;

	if (elem >= kq.kq_nfds) {
	kq_fd_t	*newtable;

		newtable = calloc(elem + 1, sizeof(kq_fd_t));
		if (!newtable)
			return -1;

		if (kq.kq_fdtable)
			memcpy(newtable, kq.kq_fdtable,
			       sizeof(kq_fd_t) * kq.kq_nfds);

		kq.kq_fdtable = newtable;
		kq.kq_nfds = (size_t)fd + 1;
	}

	assert(!KFD_IS_OPEN(&kq.kq_fdtable[fd]));
	kq.kq_fdtable[fd].kf_flags = KQF_OPEN;
	return 0;
}

int
kqsocket(int domain, int type, int protocol) {
int	fd;
	if ((fd = socket(domain, type, protocol)) == -1)
		return -1;

	if (kqopen(fd) == -1) {
		close(fd);
		return -1;
	}

	return fd;
}

int
kqclose(int fd) {
kq_fd_t	*kfd = kq_get_fd(fd);

	assert(KFD_IS_OPEN(kfd));
	kfd->kf_flags &= ~KQF_OPEN;
	return close(fd);
}


static kq_fd_t *
kq_get_fd(int fd) {
	assert((size_t)fd < kq.kq_nfds);
	assert(KFD_IS_OPEN(&kq.kq_fdtable[fd]));
	return &kq.kq_fdtable[fd];
}

int
kqinit(void) {
	time(&current_time);

	memset(&kq, 0, sizeof(kq));

	if ((kq.kq_fd = kqueue()) == -1) {
		return -1;
	}

	return 0;
}

int
kqonread(int fd, kqonreadcb readh, void *udata) {
struct kevent	 ev;
struct kq_fd	*kfd;

	kfd = kq_get_fd(fd);
	assert(kfd);

	kfd->kf_readh = readh;
	kfd->kf_udata = udata;

	memset(&ev, 0, sizeof(ev));
	ev.ident = (uintptr_t)fd;
	ev.filter = EVFILT_READ;
	ev.flags = EV_ADD | EV_ENABLE | EV_ONESHOT;
	ev.udata = (void *)(uintptr_t)fd;

	if (kevent(kq.kq_fd, &ev, 1, NULL, 0, NULL) != 0)
		return -1;

	return 0;
}

struct kq_timer {
	kqtimercb	 kt_callback;
	int		 kt_when;
	unsigned	 kt_unit;
	void		*kt_udata;
};

int
kqtimer(int when, unsigned unit, kqtimercb handler, void *udata) {
struct kevent	 ev;
struct kq_timer	*timer;

	if ((timer = calloc(1, sizeof(*timer))) == NULL)
		return -1;

	timer->kt_callback = handler;
	timer->kt_when = when;
	timer->kt_unit = unit;
	timer->kt_udata = udata;

	memset(&ev, 0, sizeof(ev));
	ev.ident = (uintptr_t)handler;
	ev.filter = EVFILT_TIMER;
	ev.fflags = unit;
	ev.data = when;
	ev.flags = EV_ADD | EV_ENABLE | EV_ONESHOT;
	ev.udata = timer;

	if (kevent(kq.kq_fd, &ev, 1, NULL, 0, NULL) != 0)
		return -1;

	return 0;
}

static int
kq_dispatch_event(struct kevent *ev) {
	if (ev->filter == EVFILT_READ) {
	int	 fd = (int)(uintptr_t)ev->udata;
	kq_fd_t	*kfd = kq_get_fd(fd);

		assert(kfd);
		assert(kfd->kf_readh);

		switch (kfd->kf_readh((int)ev->ident, kfd->kf_udata)) {
		case KQ_REARM:
			/* the kfd may have moved in memory */
			kfd = kq_get_fd(fd);

			if (kqonread((int)ev->ident, kfd->kf_readh,
				     kfd->kf_udata) == -1) {
				nlog(NLOG_ERROR, "kq_dispatch_event: "
				     "failed to rearm read");
				return -1;
			}
			break;

		case KQ_STOP:
			break;

		default:
			panic("kq_dispatch_event: unknown return from "
				"kf_readh");
		}
	} else if (ev->filter == EVFILT_TIMER) {
	struct kq_timer	*timer = ev->udata;

		assert(timer->kt_callback);

		switch (timer->kt_callback(timer->kt_udata)) {
		case KQ_REARM:
			if (kqtimer(timer->kt_when,
				    timer->kt_unit,
				    timer->kt_callback,
				    timer->kt_udata) == -1) {
				nlog(NLOG_ERROR, "kq_dispatch_event: "
				     "failed to rearm timer");
				return -1;
			}
			break;

		case KQ_STOP:
			free(timer);
			break;

		default:
			panic("kq_dispatch_event: unknown return from "
			      "timer callback");
		}
	} else {
		nlog(NLOG_DEBUG, "kq_dispatch_event: no known filters");
	}

	return 0;
}

int
kqrun(void) {
struct kevent	 ev;
int		 n;

	nlog(NLOG_INFO, "running");

	while ((n = kevent(kq.kq_fd, NULL, 0, &ev, 1, NULL)) != -1) {
		time(&current_time);

		nlog(NLOG_DEBUG, "kqrun: got event");

		if (n) {
			if (kq_dispatch_event(&ev) == -1) {
				nlog(NLOG_FATAL, "kqrun: dispatch failed");
				return -1;
			}
		}

		nlog(NLOG_DEBUG, "kqrun: sleeping");
	}

	nlog(NLOG_FATAL, "kqrun: kqueue failed: %s", strerror(errno));
	return -1;
}

struct kqaccept4data {
	kqaccept4cb nonnull	callback;
	int			flags;
	void *nullable		udata;
};

static kqdisp
kqdoaccept(int server_fd, void *udata) {
int			 newfd;
struct sockaddr_storage	 addr;
socklen_t		 addrlen = sizeof(addr);
struct kqaccept4data	*data = udata;
kqdisp			 disp = KQ_STOP;

	assert(data);
	assert(data->callback);

	if ((newfd = accept4(server_fd, (struct sockaddr *)&addr, &addrlen,
			     data->flags)) == -1) {
		disp = data->callback(server_fd, -1, NULL, 0, data->udata);
		goto done;
	}

	if (kqopen(newfd) == -1) {
		close(newfd);
		disp = data->callback(server_fd, -1, NULL, 0, data->udata);
		goto done;
	}

	disp = data->callback(server_fd, newfd,
			      (struct sockaddr *)&addr, addrlen,
			      data->udata);

done:
	if (disp == KQ_STOP)
		free(data);

	return disp;
}

int
kqaccept4(int server_fd, int flags, kqaccept4cb callback, void *udata) {
struct kqaccept4data	*data = NULL;

	if ((data = calloc(1, sizeof(*data))) == NULL)
		return -1;

	data->callback = callback;
	data->flags = flags;
	data->udata = udata;

	if (kqonread(server_fd, kqdoaccept, data) == -1) {
		free(data);
		return -1;
	}

	return 0;
}

/**
 * kqread()
 */

struct kqreaddata {
	kqreadcb nonnull	callback;
	void *nonnull		buffer;
	ssize_t			bufsize;
	void *nullable		udata;
};

kqdisp
kqdoread(int fd, void *udata) {
struct kqreaddata	*data = udata;
ssize_t			 n = 0;

	assert(fd >= 0);
	assert(data);

	n = read(fd, data->buffer, (size_t)data->bufsize);
	if (n == -1) {
		data->callback(fd, -errno, data->udata);
		goto done;
	}

	data->callback(fd, n, data->udata);

done:
	free(data);
	return KQ_STOP;
}

void
kqread(int fd, void *buf, ssize_t bufsize, kqreadcb handler, void *udata) {
struct kqreaddata	*data;

	assert(fd >= 0);
	assert(buf);
	assert(bufsize > 0);
	assert(handler);

	/* TODO: call the handler for errors here */

	if ((data = calloc(1, sizeof(*data))) == NULL)
		panic("kqread: out of memory");

	data->callback = handler;
	data->buffer = buf;
	data->bufsize = bufsize;
	data->udata = udata;

	if (kqonread(fd, kqdoread, data) == -1)
		panic("kqread: kqonread failed");

	return;
}

/**
 * kqrecvmsg()
 */

struct kqrecvmsgdata {
	kqrecvmsgcb nonnull	callback;
	void *nonnull		buffer;
	ssize_t			bufsize;
	ssize_t			nbytes;
	void *nullable		udata;
};

kqdisp
kqdorecvmsg(int fd, void *udata) {
struct kqrecvmsgdata	*data = udata;
kqdisp			 disp;

	assert(fd >= 0);
	assert(data);

	nlog(NLOG_DEBUG, "kqdorecvmsg: begin");

	/*
	 * read data until EAGAIN or MSG_EOR.
	 */
	for (;;) {
	struct msghdr	 msg;
	struct iovec	 iov;
	ssize_t		 n;

		nlog(NLOG_DEBUG, "kqdorecvmsg: in loop");

		assert(data->bufsize > data->nbytes);

		iov.iov_base = (char *)data->buffer + data->nbytes;
		iov.iov_len = (size_t)data->bufsize - (size_t)data->nbytes;

		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		n = recvmsg(fd, &msg, 0);
		nlog(NLOG_DEBUG, "kqdorecvmsg: read %d errno=%s",
		     (int)n, strerror(errno));

		if (n == -1) {
			if (errno == EAGAIN)
				return KQ_REARM;

			disp = data->callback(fd, -errno, 0, data->udata);
			break;
		}

		data->nbytes += n;
		if (msg.msg_flags & MSG_EOR) {
			nlog(NLOG_DEBUG, "kqdorecvmsg: got MSG_EOR");
			disp = data->callback(fd, data->nbytes, msg.msg_flags,
					      data->udata);
			break;
		}

		if (data->nbytes == data->bufsize) {
			disp = data->callback(fd, -ENOSPC, 0, data->udata);
			break;
		}
	}

	nlog(NLOG_DEBUG, "kqdorecvmsg: done, disp=%d", (int) disp);

	if (disp == KQ_STOP)
		free(data);
	else
		data->nbytes = 0;

	return disp;
}

void
kqrecvmsg(int fd, void *buf, ssize_t bufsize, kqrecvmsgcb handler,
	  void *udata) {
struct kqrecvmsgdata	*data;

	assert(fd >= 0);
	assert(buf);
	assert(bufsize > 0);
	assert(handler);

	/* TODO: call the handler for errors here */

	if ((data = calloc(1, sizeof(*data))) == NULL)
		panic("kqrecvmsg: out of memory");

	data->callback = handler;
	data->buffer = buf;
	data->bufsize = bufsize;
	data->udata = udata;

	if (kqonread(fd, kqdorecvmsg, data) == -1)
		panic("kqrecvmsg: kqonread failed");

	return;
}
