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
	kqreadcb	 kf_readh;
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
kqread(int fd, kqreadcb readh, void *udata) {
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

			if (kqread((int)ev->ident, kfd->kf_readh,
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
