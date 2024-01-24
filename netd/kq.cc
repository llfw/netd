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

module;

#include	<sys/types.h>
#include	<sys/event.h>
#include	<sys/socket.h>

#include	<cassert>
#include	<cerrno>
#include	<cstdlib>
#include	<cstring>
#include	<cstdio>
#include	<ctime>
#include	<coroutine>
#include	<span>
#include	<unistd.h>

#include	<new>
#include	<vector>
#include	<expected>

#include	"netd.hh"
#include	"defs.hh"

import log;
import task;

module kq;

namespace netd::kq {

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

void
dispatch(dispatchcb handler) {
	jobs.emplace_back(handler);
}

namespace {

auto runjobs(void) -> void {
	/* the job list can be appended to while we're iterating */
	for (std::size_t i = 0; i < jobs.size(); ++i)
		jobs[i]();

	jobs.clear();
}

}

auto open(int fd) -> std::expected<void, std::error_code> {
size_t	elem = (size_t)fd;

	if (elem >= kq.kq_fdtable.size())
		kq.kq_fdtable.resize(elem + 1);

	assert(!KFD_IS_OPEN(&kq.kq_fdtable[elem]));
	kq.kq_fdtable[elem].kf_flags = KQF_OPEN;
	return {};
}

auto socket(int domain, int type, int protocol)
	-> std::expected<int, std::error_code>
{
int	fd;
	if ((fd = ::socket(domain, type, protocol)) == -1)
		return std::unexpected(std::make_error_code(std::errc(errno)));

	if (auto ret = open(fd); !ret) {
		::close(fd);
		return std::unexpected(ret.error());
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

auto onread(int fd, onreadcb &&readh) -> std::expected<void, std::error_code> {
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
		return std::unexpected(std::make_error_code(std::errc(errno)));

	return {};
}

auto onread(int fd, onreadcb const &readh)
	-> std::expected<void, std::error_code>
{
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

auto timer(std::chrono::nanoseconds when, reltimercb callback)
	-> std::expected<void, std::error_code>
{
struct kevent	 ev;
reltimer	*timer;

	if ((timer = new (std::nothrow) reltimer) == NULL)
		return std::unexpected(std::make_error_code(std::errc(ENOMEM)));

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
		return std::unexpected(std::make_error_code(std::errc(errno)));

	return {};
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

namespace {

auto kq_dispatch_event(struct kevent *ev)
	-> std::expected<void, std::error_code>
{
	if (ev->filter == EVFILT_READ) {
	int	 fd = (int)(uintptr_t)ev->udata;
	kqfd	*kfd = kq_get_fd(fd);

		assert(kfd);
		assert(kfd->kf_readh);

		auto handler = std::move(kfd->kf_readh);

		switch (handler(fd)) {
		case disp::rearm:
			if (auto ret = onread(fd, std::move(handler)); !ret) {
				log::error("kq_dispatch_event: "
					   "failed to rearm read");
				return std::unexpected(ret.error());
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
				auto ret = timer(tmr->rt_duration,
						 tmr->rt_callback);
				if (!ret) {
					log::error("kq_dispatch_event: "
						   "failed to rearm timer");
					return std::unexpected(ret.error());
				} else
					delete tmr;
			}
		}
	} else {
		log::debug("kq_dispatch_event: no known filters");
	}

	return {};
}

} // anonymous namespace

auto run(void) -> std::expected<void, std::error_code> {
struct kevent	 ev;
int		 n;

	log::info("running");

	// run any jobs that were added before we started
	runjobs();

	while ((n = kevent(kq.kq_fd, NULL, 0, &ev, 1, NULL)) != -1) {
		time(&current_time);

		log::debug("kqrun: got event");

		if (n) {
			if (auto ret = kq_dispatch_event(&ev); !ret) {
				log::fatal("kqrun: dispatch failed");
				return std::unexpected(ret.error());
			}
		}

		/* handle the kqdispatch() queue */
		log::debug("kqrun: running jobs");
		runjobs();

		log::debug("kqrun: sleeping");
	}

	log::fatal("kqrun: kqueue failed: {}", strerror(errno));
	return std::unexpected(std::make_error_code(std::errc(errno)));
}

/******************************************************************************
 *
 * the kq async interface.
 *
 */

struct wait_readable {
	int _fd;

	explicit wait_readable(int fd)
	: _fd(fd)
	{ }

	auto await_ready() -> bool {
		log::debug("wait_readable: await_ready() this={}",
			   static_cast<void *>(this));
		return false;
	}

	template <typename P>
	auto await_suspend(std::coroutine_handle<P> coro) -> bool {
		log::debug("wait_readable: await_suspend() this={}",
			   static_cast<void *>(this));

		onread(_fd, [coro = std::move(coro)] (int) {
			log::debug("wait_readable: onread() callback");

			dispatch([coro = std::move(coro)] {
				log::debug("wait_readable: dispatched");
				coro.resume();
			});

			return disp::stop;
		});

		return true;
	}

	void await_resume() {
		log::debug("wait_readable: await_resume() this={}",
			   static_cast<void *>(this));
	}
};

auto readable(int fd) -> task<void> {
	co_await wait_readable(fd);
	co_return;
}

struct wait_timer {
	std::chrono::nanoseconds _duration;

	explicit wait_timer(std::chrono::nanoseconds duration)
	: _duration(duration)
	{ }

	auto await_ready() -> bool {
		log::debug("wait_timer: await_ready() this={}",
			   static_cast<void *>(this));
		return false;
	}

	template <typename P>
	auto await_suspend(std::coroutine_handle<P> coro) -> bool {
		log::debug("wait_timer: await_suspend() this={}",
			   static_cast<void *>(this));

		timer(_duration, [coro = std::move(coro)] {
			log::debug("wait_timer: onread() callback");

			dispatch([coro = std::move(coro)] {
				log::debug("wait_timer: dispatched");
				coro.resume();
			});

			return disp::stop;
		});

		return true;
	}

	void await_resume() {
		log::debug("wait_readable: await_resume() this={}",
			   static_cast<void *>(this));
	}
};

auto sleep(std::chrono::nanoseconds duration) -> task<void> {
	co_await wait_timer(duration);
}

auto run_task(task<void> &&tsk) -> void {
	auto tsk_ = new (std::nothrow) task(std::move(tsk));
	log::debug("run_task: start, task={}", static_cast<void *>(tsk_));

	// TODO: free the task
	dispatch([=] {
		log::debug("run_task: dispatched, task={}",
			   static_cast<void *>(tsk_));
		tsk_->coro_handle.resume();
	});
}

auto read(int fd, std::span<std::byte> buf)
	-> task<std::expected<std::size_t, std::error_code>>
{
	assert(fd >= 0);
	assert(buf.size() > 0);

	co_await wait_readable(fd);

	auto n = ::read(fd, buf.data(), buf.size());
	if (n < 0)
		co_return std::unexpected(std::make_error_code(
				std::errc(errno)));

	co_return n;
}

auto recvmsg(int fd, std::span<std::byte> buf)
	-> task<std::expected<std::size_t, std::error_code>>
{
	log::debug("async_recvmsg: begin");

	// the remaining buffer we can read into
	auto bufleft = buf;

	/* read until we get either MSG_EOR or run out of buffer space */
	for (;;) {
	struct msghdr	 msg;
	struct iovec	 iov;
	ssize_t		 n;

		if (bufleft.size() == 0)
			// out of space
			co_return std::unexpected(
					std::make_error_code(
						std::errc(ENOSPC)));

		iov.iov_base = bufleft.data();
		iov.iov_len = bufleft.size();

		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		// wait for some data to arrive
		log::debug("async_recvmsg: waiting for fd to be readable");
		co_await wait_readable(fd);
		log::debug("async_recvmsg: fd is readable");

		n = ::recvmsg(fd, &msg, 0);
		log::debug("kqdorecvmsg: read {} errno={}",
		     n, strerror(errno));

		if (n == 0)
			// end of file
			co_return 0u;

		if (n == -1)
			// other error
			co_return std::unexpected(
					std::make_error_code(
						std::errc(errno)));

		bufleft = bufleft.subspan(static_cast<std::size_t>(n));

		if (!(msg.msg_flags & MSG_EOR))
			// we didn't get the complete message, try again
			continue;

		co_return buf.size() - bufleft.size();
	}
}

auto accept4(int server_fd, sockaddr *, socklen_t *, int flags)
	-> task<std::expected<int, std::error_code>>
{
	// wait for the fd to become readable
	co_await wait_readable(server_fd);

	sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);

	int newfd = ::accept4(server_fd, (sockaddr *)&addr, &addrlen, flags);
	if (newfd == -1)
		co_return std::unexpected(
			std::make_error_code(std::errc(errno)));

	if (auto ret = open(newfd); !ret) {
		::close(newfd);
		co_return std::unexpected(ret.error());
	}

	co_return newfd;
}

} // namespace netd::kq
