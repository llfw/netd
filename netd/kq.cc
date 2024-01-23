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

#include	<new>
#include	<vector>

#include	"netd.hh"
#include	"kq.hh"
#include	"log.hh"

namespace kq {

time_t current_time;

/* kq_fd represents a single open fd */
#define	KQF_OPEN	0x1u
#define	KFD_IS_OPEN(kfd)	(((kfd)->kf_flags & KQF_OPEN) == KQF_OPEN)

struct kqfd {
	onreadcb	kf_readh;
	uint8_t		kf_flags = 0;
};

static kqfd *nonnull kq_get_fd(int fd);

/* kq represents a kqueue instance */
struct kqueue {
	int			kq_fd = 0;
	std::vector<kqfd>	kq_fdtable;
};

/* the global instance */
namespace {
	struct kqueue kq;
}

/*
 * list of jobs to dispatch at the end of the event loop.
 */
static std::vector<dispatchcb> jobs;

disp kqdoread(int fd, void *nullable udata);
disp kqdorecvmsg(int fd, void *nullable udata);

void
dispatch(dispatchcb handler) {
	jobs.emplace_back(handler);
}

static void
runjobs(void) {
	/* the job list can be appended to while we're iterating */
	for (std::size_t i = 0; i < jobs.size(); ++i)
		jobs[i]();

	jobs.clear();
}

int
open(int fd) {
size_t	elem = (size_t)fd;

	if (elem >= kq.kq_fdtable.size())
		kq.kq_fdtable.resize(elem + 1);

	assert(!KFD_IS_OPEN(&kq.kq_fdtable[elem]));
	kq.kq_fdtable[elem].kf_flags = KQF_OPEN;
	return 0;
}

int
socket(int domain, int type, int protocol) {
int	fd;
	if ((fd = ::socket(domain, type, protocol)) == -1)
		return -1;

	if (open(fd) == -1) {
		::close(fd);
		return -1;
	}

	return fd;
}

int
close(int fd) {
kqfd	*kfd = kq_get_fd(fd);

	assert(KFD_IS_OPEN(kfd));
	kfd->kf_flags &= ~KQF_OPEN;
	return ::close(fd);
}


static kqfd *
kq_get_fd(int fd) {
size_t	elem = (size_t)fd;

	assert((size_t)fd < kq.kq_fdtable.size());
	assert(KFD_IS_OPEN(&kq.kq_fdtable[elem]));
	return &kq.kq_fdtable[elem];
}

int
init(void) {
	time(&current_time);

	if ((kq.kq_fd = ::kqueue()) == -1) {
		return -1;
	}

	return 0;
}

int
onread(int fd, onreadcb &&readh) {
struct kevent	 ev;
kqfd		*kfd;

	kfd = kq_get_fd(fd);
	assert(kfd);

	kfd->kf_readh = std::move(readh);

	memset(&ev, 0, sizeof(ev));
	ev.ident = (uintptr_t)fd;
	ev.filter = EVFILT_READ;
	ev.flags = EV_ADD | EV_ENABLE | EV_ONESHOT;
	ev.udata = (void *)(uintptr_t)fd;

	if (kevent(kq.kq_fd, &ev, 1, NULL, 0, NULL) != 0)
		return -1;

	return 0;
}

int
onread(int fd, onreadcb const &readh) {
	auto readcopy = readh;
	return onread(fd, std::move(readcopy));
}

/*
 * create a relative timer.
 */

struct reltimer {
	reltimercb			rt_callback;
	std::chrono::nanoseconds	rt_duration;
};

int
timer(std::chrono::nanoseconds when, reltimercb callback) {
struct kevent	 ev;
reltimer	*timer;

	if ((timer = new (std::nothrow) reltimer) == NULL)
		return -1;

	timer->rt_callback = callback;
	timer->rt_duration = when;

	memset(&ev, 0, sizeof(ev));
	ev.ident = (uintptr_t)timer;
	ev.filter = EVFILT_TIMER;
	ev.fflags = NOTE_NSECONDS;
	ev.data = std::chrono::duration_cast<
			std::chrono::nanoseconds
		>(when).count();
	ev.flags = EV_ADD | EV_ENABLE | EV_ONESHOT;
	ev.udata = timer;

	if (kevent(kq.kq_fd, &ev, 1, NULL, 0, NULL) != 0)
		return -1;

	return 0;
}

/*
 * create an absolute timer.
 */

struct abstimer {
	abstimercb	at_callback;
};

int
timer(std::chrono::time_point<std::chrono::system_clock> when,
      abstimercb callback) {
struct kevent	 ev;
abstimer	*timer;
using namespace std::literals;

	if ((timer = new (std::nothrow) abstimer) == NULL)
		return -1;

	timer->at_callback = callback;

	memset(&ev, 0, sizeof(ev));
	ev.ident = (uintptr_t)timer;
	ev.filter = EVFILT_TIMER;
	ev.fflags = NOTE_ABSTIME | NOTE_NSECONDS;
	ev.data = std::chrono::duration_cast<
			std::chrono::nanoseconds
		>(when.time_since_epoch()).count();
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
	kqfd	*kfd = kq_get_fd(fd);

		assert(kfd);
		assert(kfd->kf_readh);

		auto handler = std::move(kfd->kf_readh);

		switch (handler(fd)) {
		case disp::rearm:
			if (onread(fd, std::move(handler)) == -1) {
				nlog::error("kq_dispatch_event: "
				     "failed to rearm read");
				return -1;
			}
			break;

		case disp::stop:
			break;

		default:
			panic("kq_dispatch_event: unknown return from "
				"kf_readh");
		}
	} else if (ev->filter == EVFILT_TIMER) {
		if (ev->fflags & NOTE_ABSTIME) {
		abstimer	*tmr = static_cast<abstimer *>(ev->udata);
			tmr->at_callback();
		} else {
		reltimer	*tmr = static_cast<reltimer *>(ev->udata);

			/* TODO: we lose time when rescheduling here */
			auto disp = tmr->rt_callback();

			if (disp == disp::rearm) {
				if (timer(tmr->rt_duration,
					  tmr->rt_callback) == -1) {
					nlog::error("kq_dispatch_event: "
					     "failed to rearm timer");
					return -1;
				} else
					delete tmr;
			}
		}
	} else {
		nlog::debug("kq_dispatch_event: no known filters");
	}

	return 0;
}

int
run(void) {
struct kevent	 ev;
int		 n;

	nlog::info("running");

	while ((n = kevent(kq.kq_fd, NULL, 0, &ev, 1, NULL)) != -1) {
		time(&current_time);

		nlog::debug("kqrun: got event");

		if (n) {
			if (kq_dispatch_event(&ev) == -1) {
				nlog::fatal("kqrun: dispatch failed");
				return -1;
			}
		}

		/* handle the kqdispatch() queue */
		nlog::debug("kqrun: running jobs");
		runjobs();

		nlog::debug("kqrun: sleeping");
	}

	nlog::fatal("kqrun: kqueue failed: {}", strerror(errno));
	return -1;
}

int
accept4(int server_fd, int flags, accept4cb callback) {
	auto handler = [=](int) {
	sockaddr_storage	 addr;
	socklen_t		 addrlen = sizeof(addr);

		int newfd = accept4(server_fd,
				    (struct sockaddr *)&addr,
				    &addrlen,
				    flags);
		if (newfd == -1)
			return callback(server_fd, -1, NULL, 0);

		if (open(newfd) == -1) {
			::close(newfd);
			return callback(server_fd, -1, NULL, 0);
		}

		return callback(server_fd, newfd, (struct sockaddr *)&addr,
				addrlen);
	};

	if (onread(server_fd, handler) == -1)
		return -1;

	return 0;
}

/**
 * kqread()
 */

struct readdata {
	readcb		callback;
	std::span<char>	buffer;
};

void
read(int fd, std::span<std::byte> buf, readcb callback) {
	assert(fd >= 0);
	assert(buf.size() > 0);
	assert(callback);

	auto handler = [=](int fd) {
		if (auto n = ::read(fd, buf.data(), buf.size()); n >= 0)
			callback(fd, n);
		else
			callback(fd, -errno);

		return disp::stop;
	};

	if (onread(fd, handler) == -1)
		dispatch([=]() {
			callback(fd, -errno);
		});
}

/**
 * kqrecvmsg()
 */

struct recvmsgdata {
	recvmsgcb		callback;
	std::span<std::byte>	buffer;
	size_t			nbytes = 0;
};

static disp
dorecvmsg(int fd, recvmsgdata &data) {
disp	disp;

	nlog::debug("kqdorecvmsg: begin");

	/*
	 * read data until EAGAIN or MSG_EOR.
	 */
	for (;;) {
	struct msghdr	 msg;
	struct iovec	 iov;
	ssize_t		 n;

		nlog::debug("kqdorecvmsg: in loop");

		assert(data.buffer.size() > data.nbytes);

		iov.iov_base = &data.buffer[0] + data.nbytes;
		iov.iov_len = data.buffer.size() - data.nbytes;

		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		n = ::recvmsg(fd, &msg, 0);
		nlog::debug("kqdorecvmsg: read {} errno={}",
		     n, strerror(errno));

		if (n == 0) {
			disp = data.callback(fd, 0, 0);
			break;
		}

		if (n == -1) {
			if (errno == EAGAIN)
				return disp::rearm;

			disp = data.callback(fd, -errno, 0);
			break;
		}

		data.nbytes += (std::size_t)n;
		if (msg.msg_flags & MSG_EOR) {
			nlog::debug("kqdorecvmsg: got MSG_EOR");
			disp = data.callback(fd, (ssize_t)data.nbytes,
					     msg.msg_flags);
			break;
		}

		if (data.nbytes == data.buffer.size()) {
			/* ran out of buffer space */
			disp = data.callback(fd, -ENOSPC, 0);
			break;
		}
	}

	nlog::debug("kqdorecvmsg: done, disp={}", static_cast<int>(disp));

	if (disp == disp::rearm)
		data.nbytes = 0;

	return disp;
}

void
recvmsg(int fd, std::span<std::byte> buf, recvmsgcb callback) {
	assert(fd >= 0);
	assert(buf.size() > 0);
	assert(callback);

	recvmsgdata data{ callback, buf };

	auto handler = [=](int fd) mutable {
		return dorecvmsg(fd, data);
	};

	if (onread(fd, handler) == -1)
		dispatch([=]() {
			callback(fd, -errno, 0);
		});

	return;
}

} // namespace kq
