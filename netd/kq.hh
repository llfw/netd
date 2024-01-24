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

#ifndef	NETD_KQ_H_INCLUDED
#define	NETD_KQ_H_INCLUDED

/*
 * kq: lightweight wrapper around kqueue()/kevent() that allows users to
 * register handlers for various events.
 */

#include	<sys/types.h>
#include	<sys/socket.h>

#include	<functional>
#include	<span>
#include	<chrono>
#include	<expected>

#include	"defs.hh"
#include	"task.hh"
#include	"log.hh"

namespace netd::kq {

/*
 * the current time.  this is updated every time the event loop runs, before
 * events are dispatched.
 */
extern time_t current_time;

/* event handler return value */
enum class disp {
	rearm,	/* continue listening for this event */
	stop,	/* stop listening */
};

/* initialise kq */
int init(void);

/* start the kq runner.  only returns on failure. */
auto run(void) -> std::expected<void, std::error_code>;

/* call handler() at the end of the current event loop */
using dispatchcb = std::function<void ()>;
void dispatch(dispatchcb handler);

/* register an fd in kqueue.  this must be done before using it. */
auto open(int fd) -> std::expected<void, std::error_code>;

/* create a socket and register it with kq in a single operation */
auto socket(int, int, int) -> std::expected<int, std::error_code>;

/* unregister an fd, close it and cancel any pending events */
int close(int fd);

/******************************************************************************
 * synchronous interface
 */

/*
 * register for read events on an fd.  the handler can return disp::rearm to
 * continue listening for events, or disp::stop to remove the registration.
 */
using onreadcb = std::function<disp (int fd)>;
auto onread(int fd, onreadcb const &) -> std::expected<void, std::error_code>;
auto  onread(int fd, onreadcb &&) -> std::expected<void, std::error_code>;

/*
 * register a timer that fires after 'duration'.  returning kq_rearm will
 * rearm the timer with the same duration.
 */
using reltimercb = std::function<disp ()>;
auto timer(std::chrono::nanoseconds, reltimercb)
	-> std::expected<void, std::error_code>;

template<typename Rep, typename Period>
	requires (!std::is_same_v<Period, std::nano>)
auto timer(std::chrono::duration<Rep, Period> duration, reltimercb handler) {
	return timer(
		std::chrono::duration_cast<std::chrono::nanoseconds>(duration),
		handler);
}

/*
 * register a timer that fires at the given time.  this timer cannot be
 * rearmed.
 */
using abstimercb = std::function<void ()>;
int timer(std::chrono::time_point<std::chrono::system_clock>, abstimercb);

/******************************************************************************
 * async (coroutine) interface.
 */

/*
 * start an async task in the background.  the task will run until completion,
 * then be destroyed.
 */
auto run_task(task<void> &&task) -> void;

/*
 * suspend until the given fd becomes readable.
 */
auto readable(int fd) -> task<void>;

/*
 * sleep until the given timer expires.
 */
auto sleep(std::chrono::nanoseconds) -> task<void>;

template<typename Rep, typename Period>
auto sleep(std::chrono::duration<Rep, Period> duration) -> task<void> {
	co_await sleep(std::chrono::duration_cast<
				std::chrono::nanoseconds
			>(duration));
}

/*
 * read data into the provided buffer.
 */
auto read(int fd, std::span<std::byte> buf)
	-> task<std::expected<std::size_t, std::error_code>>;

/*
 * read a single message into the provided buffer.  returns the size of the
 * message read, or error.  if the buffer is too small to hold the message, the
 * message is discarded and ENOSPC is returned.
 */
[[nodiscard]] auto recvmsg(int fd, std::span<std::byte> buf) ->
	task<std::expected<std::size_t, std::error_code>>;

/*
 * accept a connection on the given server socket.  the arguments are as
 * described in accept4(2).
 *
 * the fd will have kq::open() called on it automatically.
 */
[[nodiscard]]
auto accept4(int server_fd, sockaddr *, socklen_t *, int flags)
	-> task<std::expected<int, std::error_code>>;

} // namespace netd::kq

#endif	/* !NETD_KQ_H_INCLUDED */
