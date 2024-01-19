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

#include	<assert.h>
#include	<errno.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdio.h>
#include	<time.h>

#include	"netd.h"
#include	"kq.h"
#include	"log.h"

time_t current_time;

struct kq_fd {
	kq_read_handler	 kf_readh;
	void		*kf_udata;
};

struct kq {
	int		 kq_fd;
	struct kq_fd	*kq_fdtable;
	size_t		 kq_nfds;
};

static struct kq_fd	*kq_get_fd(struct kq *kq, int fd);

static struct kq_fd *
kq_get_fd(struct kq *kq, int fd) {
	if ((size_t)fd >= kq->kq_nfds) {
		kq->kq_nfds = (size_t)fd + 1;
		kq->kq_fdtable = 
			realloc(kq->kq_fdtable,
				sizeof(struct kq_fd) * kq->kq_nfds);
		if (kq->kq_fdtable == NULL)
			panic("kq_get_fd: out of memory");
	}

	return &kq->kq_fdtable[fd];
}

struct kq *
kq_create(void) {
struct kq	*ret;

	if ((ret = calloc(1, sizeof(*ret))) == NULL)
		return NULL;

	if ((ret->kq_fd = kqueue()) == -1) {
		free(ret);
		return NULL;
	}

	return ret;
}

int
kq_register_read(struct kq *kq, int fd, kq_read_handler readh, void *udata) {
struct kevent	 ev;
struct kq_fd	*kfd;

	kfd = kq_get_fd(kq, fd);
	assert(kfd);

	kfd->kf_readh = readh;
	kfd->kf_udata = udata;

	memset(&ev, 0, sizeof(ev));
	ev.ident = (uintptr_t)fd;
	ev.filter = EVFILT_READ;
	ev.flags = EV_ADD | EV_ENABLE | EV_ONESHOT;
	ev.udata = (void *)(uintptr_t)fd;

	if (kevent(kq->kq_fd, &ev, 1, NULL, 0, NULL) != 0)
		return -1;

	return 0;
}

static int
kq_dispatch_event(struct kq *kq, struct kevent *ev) {
	if ((ev->filter & EVFILT_READ) != 0) {
	int		 fd = (int)(uintptr_t)ev->udata;
	struct kq_fd	*kfd = kq_get_fd(kq, fd);

		assert(kfd);
		assert(kfd->kf_readh);

		switch (kfd->kf_readh(kq, (int)ev->ident, kfd->kf_udata)) {
		case KQ_REARM:
			if (kq_register_read(kq, (int)ev->ident, kfd->kf_readh,
					     kfd->kf_udata) == -1) {
				nlog(NLOG_ERROR, "kq_dispatch_event: "
					"failed to rearm event");
				return -1;
			}
			break;

		case KQ_STOP:
			break;

		default:
			panic("kq_dispatch_event: unknown return from "
				"kf_readh");
		}
	} else {
		nlog(NLOG_DEBUG, "kq_run: no known filters");
	}

	return 0;
}

int
kq_run(struct kq *kq) {
struct kevent	 ev;
int		 n;

	nlog(NLOG_INFO, "running");

	while ((n = kevent(kq->kq_fd, NULL, 0, &ev, 1, NULL)) != -1) {
		time(&current_time);

		nlog(NLOG_DEBUG, "kq_run: got event");

		if (n) {
			if (kq_dispatch_event(kq, &ev) == -1) {
				nlog(NLOG_FATAL, "kq_run: dispatch failed");
				return -1;
			}
		}

		nlog(NLOG_DEBUG, "kq_run: sleeping");
	}

	nlog(NLOG_FATAL, "kqueue failed: %s", strerror(errno));
	return -1;
}
