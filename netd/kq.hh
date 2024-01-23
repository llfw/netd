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

#include	"defs.hh"

namespace kq {

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
int run(void);

/* call handler() at the end of the current event loop */
using dispatchcb = std::function<void ()>;
void dispatch(dispatchcb handler);

/* register an fd in kqueue.  this must be done before using it. */
int open(int fd);

/* create a socket and register it with kq in a single operation */
int socket(int, int, int);

/* unregister an fd, close it and cancel any pending events */
int close(int fd);

/*
 * register for read events on an fd.  the handler can return disp::rearm to
 * continue listening for events, or disp::stop to remove the registration.
 */
using onreadcb = std::function<disp (int fd)>;
int onread(int fd, onreadcb const &);
int onread(int fd, onreadcb &&);

/*
 * read data from the fd into the provided buffer and call the handler once the
 * read is complete.  if the read was successful, nbytes is set to the amount
 * of data which was read.  if an error occurred, nbytes is set to -errno and
 * the contents of buf are undefined.
 *
 * if the handler is rearmed, the read will be repeated with the same buffer.
 */
using readcb = std::function<disp (int fd, ssize_t nbytes)>;
void read(int fd, std::span<std::byte> buf, readcb);

/*
 * read one message from the fd into the given buffer.  reading will continue
 * until either the entire message is read, or the buffer is full.  if
 * successful, nbytes is set to the size of the message and flags contains the
 * recvmsg msg_flags field.  otherwise, nbytes is set to -errno and flags is
 * undefined.
 *
 * if the handler is rearmed, the read will be repeated with the same buffer.
 */
using recvmsgcb = std::function<disp (int fd, ssize_t nbytes, int flags)>;
void recvmsg(int fd, std::span<std::byte> buf, recvmsgcb handler);

/*
 * wait for a connection to be ready on the given server socket, then accept it
 * and pass it to the callback.  the arguments are as described in accept4(2).
 *
 * the fd will have kqopen() called on it automatically.
 *
 * if the accept4() call fails, client_fd will be -1, errno is set and addr is
 * NULL.
 */
using accept4cb = std::function<disp (
				int server_fd,
				int client_fd,
				sockaddr *nullable addr,
				socklen_t addrlen)>;
int accept4(int server_fd, int flags, accept4cb);

/*
 * register a timer that fires after 'duration'.  returning kq_rearm will
 * rearm the timer with the same duration.
 */
using reltimercb = std::function<disp ()>;
int timer(std::chrono::nanoseconds, reltimercb);

template<typename Rep, typename Period>
	requires (!std::is_same_v<Period, std::nano>)
int timer(std::chrono::duration<Rep, Period> duration,
	    reltimercb handler) {
	return timer(
		std::chrono::duration_cast<
			std::chrono::nanoseconds
		>(duration),
		handler);
}

/*
 * register a timer that fires at the given time.  this timer cannot be
 * rearmed.
 */
using abstimercb = std::function<void ()>;
int timer(std::chrono::time_point<std::chrono::system_clock>, abstimercb);

} // namespace kq

#endif	/* !NETD_KQ_H_INCLUDED */
