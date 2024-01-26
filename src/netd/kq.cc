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
import netd.panic;
import netd.error;

module kq;

namespace netd::kq {

/* kq represents a kqueue instance */
struct kqueue {
	int			kq_fd = 0;
};

/* the global instance */
struct kqueue kq;

/*
 * list of jobs to dispatch at the end of the event loop.
 */
static std::vector<dispatchcb> jobs;

void
dispatch(dispatchcb handler) {
	jobs.emplace_back(std::move(handler));
}

auto runjobs(void) -> void {
	/* the job list can be appended to while we're iterating */
	for (std::size_t i = 0; i < jobs.size(); ++i)
		jobs[i]();

	jobs.clear();
}

auto init(void) -> std::expected<void, std::error_code> {
	time(&current_time);

	if ((kq.kq_fd = ::kqueuex(KQUEUE_CLOEXEC)) == -1)
		return std::unexpected(error::from_errno());

	return {};
}

/*
 * make kevents awaitable
 */
struct wait_kevent {
	struct kevent &ev;

	explicit wait_kevent(struct kevent &ev_) : ev(ev_) {}

	auto await_ready() -> bool {
		log::debug("wait_kevent: await_ready() this={}",
			   static_cast<void *>(this));
		return false;
	}

	template <typename P>
	auto await_suspend(std::coroutine_handle<P> coro) -> bool {
		/*
		 * store the address of the kevent in ext[2].  this signals the
		 * kq dispatcher that it should copy the kevent there, and then
		 * resume the coro handle we place in ext[3].
		 */
		ev.ext[2] = reinterpret_cast<uintptr_t>(&ev);
		ev.ext[3] = reinterpret_cast<uintptr_t>(coro.address());
		log::debug("wait_kevent: await_suspend() this={}",
			   static_cast<void *>(this));

		if (kevent(kq.kq_fd, &ev, 1, nullptr, 0, nullptr) == -1)
			panic("wait_kevent: kevent() failed: {}",
			      std::strerror(errno));

		return true;
	}

	void await_resume() {
		log::debug("wait_kevent: await_resume() this={}",
			   static_cast<void *>(this));
	}
};

auto operator co_await (struct kevent &ev) {
	return wait_kevent(ev);
}

auto run(void) -> std::expected<void, std::error_code> {
struct kevent	 ev;
int		 n;

	auto handle = [](struct kevent &ev) {
		dispatch([=] {
			log::debug("kq_dispatch_event: resuming");

			auto evaddr = reinterpret_cast<
						struct kevent *
					>(ev.ext[2]);
			std::memcpy(evaddr, &ev, sizeof(ev));

			auto coroaddr = reinterpret_cast<void *>(ev.ext[3]);
			auto coro = std::coroutine_handle<>::from_address(
								coroaddr);
			coro.resume();
		});
	};

	log::info("running");

	// run any jobs that were added before we started
	runjobs();

	while ((n = kevent(kq.kq_fd, NULL, 0, &ev, 1, NULL)) != -1) {
		time(&current_time);

		log::debug("kqrun: got event");

		if (n) {
			if (ev.ext[2])
				handle(ev);
			else
				panic("kq_dispatch_event: unexpected event");
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

auto run_task(jtask<void> &&tsk) -> void {
	auto tsk_ = new (std::nothrow) jtask(std::move(tsk));
	log::debug("run_task: start, task={}", static_cast<void *>(tsk_));

	tsk_->on_final_suspend([=] {
		dispatch([=] {
			log::debug("run_task: task@{} is finished",
				   static_cast<void *>(tsk_));
			delete tsk_;
		});
	});

	dispatch([=] {
		log::debug("run_task: dispatched, task={}",
			   static_cast<void *>(tsk_));
		tsk_->_handle.resume();
	});
}

auto wait_readable(int fd) -> task<void> {
	struct kevent ev{};

	ev.ident = (uintptr_t)fd;
	ev.filter = EVFILT_READ;
	ev.flags = EV_ADD | EV_ENABLE | EV_ONESHOT;

	co_await ev;
}

auto wait_writable(int fd) -> task<void> {
	struct kevent ev{};

	ev.ident = (uintptr_t)fd;
	ev.filter = EVFILT_WRITE;
	ev.flags = EV_ADD | EV_ENABLE | EV_ONESHOT;

	co_await ev;
}

auto sleep(std::chrono::nanoseconds duration) -> task<void> {
	struct kevent ev{};

	ev.ident = reinterpret_cast<uintptr_t>(&ev);
	ev.filter = EVFILT_TIMER;
	ev.fflags = NOTE_NSECONDS;
	ev.data = duration.count();
	ev.flags = EV_ADD | EV_ENABLE | EV_ONESHOT;

	co_await ev;
}

auto sleep_until(std::chrono::time_point<std::chrono::system_clock> when)
	-> task<void>
{
	struct kevent ev{};

	ev.ident = reinterpret_cast<uintptr_t>(&ev);
	ev.filter = EVFILT_TIMER;
	ev.fflags = NOTE_ABSTIME | NOTE_NSECONDS;
	ev.data = std::chrono::duration_cast<
			std::chrono::nanoseconds
		>(when.time_since_epoch()).count();
	ev.flags = EV_ADD | EV_ENABLE | EV_ONESHOT;

	co_await ev;
}

auto read(int fd, std::span<std::byte> buf)
	-> task<std::expected<std::size_t, std::error_code>>
{
	assert(fd >= 0);
	assert(buf.size() > 0);

	// keep trying the read until we get some data, or an error
	for (;;) {
		auto n = ::read(fd, buf.data(), buf.size());

		if (n >= 0)
			co_return n;

		if (errno != EAGAIN)
			co_return std::unexpected(error::from_errno());

		co_await wait_readable(fd);
	}
}

auto write(int fd, std::span<std::byte const> buf)
	-> task<std::expected<std::size_t, std::error_code>>
{
	assert(fd >= 0);
	assert(buf.size() > 0);

	for (;;) {
		auto n = ::write(fd, buf.data(), buf.size());

		if (n >= 0)
			co_return n;

		if (errno != EAGAIN)
			co_return std::unexpected(error::from_errno());

		co_await wait_writable(fd);
	}
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

		if (newfd >= 0)
			co_return newfd;

		if (errno != EAGAIN)
			co_return std::unexpected(error::from_errno());

		// wait for the fd to become readable
		co_await wait_readable(server_fd);
	}
}

} // namespace netd::kq
