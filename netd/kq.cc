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

#include	"defs.hh"

import log;
import task;
import panic;
import netd.error;

module kq;

namespace netd::kq {

/* kq_fd represents a single open fd */
#define	KQF_OPEN	0x1u
#define	KFD_IS_OPEN(kfd)	(((kfd)->kf_flags & KQF_OPEN) == KQF_OPEN)

struct kqfd {
	// the task waiting for this fd to be readable
	std::coroutine_handle<>	kf_reader;
	uint8_t			kf_flags = 0;
};

static kqfd *nonnull kq_get_fd(int fd);

/* kq represents a kqueue instance */
struct kqueue {
	int			kq_fd = 0;
	std::vector<kqfd>	kq_fdtable;
};

/* the global instance */
struct kqueue kq;

/*
 * list of jobs to dispatch at the end of the event loop.
 */
static std::vector<dispatchcb> jobs;

void
dispatch(dispatchcb handler) {
	jobs.emplace_back(handler);
}

auto runjobs(void) -> void {
	/* the job list can be appended to while we're iterating */
	for (std::size_t i = 0; i < jobs.size(); ++i)
		jobs[i]();

	jobs.clear();
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

	if ((kq.kq_fd = ::kqueuex(KQUEUE_CLOEXEC)) == -1) {
		return -1;
	}

	return 0;
}

/*
 * wait for this fd to become readable, then resume the given coroutine.
 */
auto suspend_read(int fd, std::coroutine_handle<> coro) -> void {
struct kevent	 ev;
kqfd		*kfd;

	kfd = kq_get_fd(fd);
	assert(kfd);

	kfd->kf_reader = coro;

	memset(&ev, 0, sizeof(ev));
	ev.ident = (uintptr_t)fd;
	ev.filter = EVFILT_READ;
	ev.flags = EV_ADD | EV_ENABLE | EV_ONESHOT;
	ev.udata = (void *)(uintptr_t)fd;

	if (kevent(kq.kq_fd, &ev, 1, NULL, 0, NULL) != 0)
		panic("kq: kevent failed: {}", std::strerror(errno));
}

/*
 * wait for a relative timer to expire, then resume the given coroutine.
 */

struct reltimer {
	std::coroutine_handle<>	waiter;
};

auto timer(std::chrono::nanoseconds when, std::coroutine_handle<> callback)
	-> void
{
struct kevent	 ev;
reltimer	*timer;

	if ((timer = new (std::nothrow) reltimer) == NULL)
		panic("kq: out of memory");

	timer->waiter = callback;

	memset(&ev, 0, sizeof(ev));
	ev.ident = reinterpret_cast<uintptr_t>(timer);
	ev.filter = EVFILT_TIMER;
	ev.fflags = NOTE_NSECONDS;
	ev.data = std::chrono::duration_cast<
			std::chrono::nanoseconds
		>(when).count();
	ev.flags = EV_ADD | EV_ENABLE | EV_ONESHOT;
	ev.udata = timer;

	if (kevent(kq.kq_fd, &ev, 1, NULL, 0, NULL) != 0)
		panic("kq: kevent() failed: {}", std::strerror(errno));
}

/*
 * wait for an absolute timer to expire, then resume the given coroutine.
 */

struct abstimer {
	// the task waiting for this timer
	std::coroutine_handle<>	waiter;
};

auto suspend_timer(
	std::chrono::time_point<std::chrono::system_clock> when,
	std::coroutine_handle<> coro)
	-> void
{
struct kevent	 ev;
abstimer	*timer;
using namespace std::literals;

	if ((timer = new (std::nothrow) abstimer) == NULL)
		panic("kq: out of memory");

	timer->waiter = coro;

	memset(&ev, 0, sizeof(ev));
	ev.ident = reinterpret_cast<uintptr_t>(timer);
	ev.filter = EVFILT_TIMER;
	ev.fflags = NOTE_ABSTIME | NOTE_NSECONDS;
	ev.data = std::chrono::duration_cast<
			std::chrono::nanoseconds
		>(when.time_since_epoch()).count();
	ev.flags = EV_ADD | EV_ENABLE | EV_ONESHOT;
	ev.udata = timer;

	if (kevent(kq.kq_fd, &ev, 1, NULL, 0, NULL) != 0)
		panic("kq: kevent() failed: {}", std::strerror(errno));
}

auto kq_dispatch_timer(struct kevent *ev) -> void
{
	if (ev->fflags & NOTE_ABSTIME) {
		auto tmr = static_cast<abstimer *>(ev->udata);
		dispatch([=] {
			tmr->waiter.resume();
			delete tmr;
		});
	} else {
		auto tmr = static_cast<reltimer *>(ev->udata);
		dispatch([=] {
			tmr->waiter.resume();
			delete tmr;
		});
	}
}

auto kq_dispatch_read(struct kevent *ev) -> void
{
	dispatch([=] {
		auto fd = (int)(uintptr_t)ev->udata;
		auto kfd = kq_get_fd(fd);

		assert(kfd);
		assert(kfd->kf_reader);

		kfd->kf_reader.resume();
		kfd->kf_reader = {};
	});
}

auto kq_dispatch_event(struct kevent *ev) -> void
{
	if (ev->filter == EVFILT_READ)
		return kq_dispatch_read(ev);
	else if (ev->filter == EVFILT_TIMER)
		return kq_dispatch_timer(ev);
	else
		panic("kq_dispatch_event: no known filters");
}

auto run(void) -> std::expected<void, std::error_code> {
struct kevent	 ev;
int		 n;

	log::info("running");

	// run any jobs that were added before we started
	runjobs();

	while ((n = kevent(kq.kq_fd, NULL, 0, &ev, 1, NULL)) != -1) {
		time(&current_time);

		log::debug("kqrun: got event");

		if (n)
			kq_dispatch_event(&ev);

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

		suspend_read(_fd, coro);
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

		timer(_duration, coro);
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
		tsk_->_handle.resume();
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
	log::debug("kq::recvmsg: begin");

	// the remaining buffer we can read into
	auto bufleft = buf;

	// try a single read from the fd.
	auto recv1 = [&] (auto &msg) {
		auto iov = iovec{bufleft.data(), bufleft.size()};

		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;

		// wait for some data to arrive
		auto n = ::recvmsg(fd, &msg, 0);
		log::debug("kq::recvmsg: read {} errno={}",
		     n, errno ? "-" : std::strerror(errno));
		return n;
	};

	/* read until we get either MSG_EOR or run out of buffer space */
	for (;;) {
		if (bufleft.size() == 0)
			// out of space
			co_return std::unexpected(error::from_errno(ENOSPC));

		auto msg = msghdr{};

		switch (auto n = recv1(msg)) {
		case 0:
			// end of file
			co_return 0u;

		case -1:
			if (errno != EAGAIN)
				co_return std::unexpected(error::from_errno());

			log::debug("kq::recvmsg: waiting for fd"
				   " to be readable");
			co_await wait_readable(fd);
			log::debug("kq::recvmsg: fd is readable");
			break;

		default:
			// we read some data, adjust the remaining buffer space
			bufleft = bufleft.subspan(static_cast<std::size_t>(n));

			if (msg.msg_flags & MSG_EOR)
				// we read an entire message
				co_return buf.size() - bufleft.size();
			break;
		}

		// we didn't get the complete message, try again
	}
}

auto accept4(int server_fd, sockaddr *addr, socklen_t *addrlen, int flags)
	-> task<std::expected<int, std::error_code>>
{
	for (;;) {
		// try accepting forever until we get an error, or succeed.
		auto newfd = ::accept4(server_fd, addr, addrlen, flags);

		if (newfd >= 0) {
			if (auto ret = open(newfd); !ret) {
				::close(newfd);
				co_return std::unexpected(ret.error());
			}

			co_return newfd;
		}

		if (errno != EAGAIN)
			co_return std::unexpected(error::from_errno());

		// wait for the fd to become readable
		co_await wait_readable(server_fd);
	}
}

} // namespace netd::kq
