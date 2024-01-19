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

/*
 * the current time.  this is updated every time the event loop runs, before
 * events are dispatched.
 */
extern time_t current_time;

/* event handler return value */
typedef enum {
	KQ_REARM,	/* continue listening for this event */
	KQ_STOP,	/* stop listening */
} kqdisp;

/* reference to a kq instance */
struct kq;
typedef struct kq kq_t;

/* create a new kqueue instance */
kq_t		 *kqnew		(void);

/* start the kq runner.  only returns on failure. */
int		 kqrun		(kq_t *);

/*
 * register for read events on an fd.  return KQ_REARM to continue listening
 * for events, or KQ_STOP to remove the registration.
 */
typedef kqdisp (*kqreadcb) (kq_t *, int fd, void *udata);
int		 kqread		(kq_t *, int fd,
				 kqreadcb reader, void *udata);

/*
 * register a timer event that fires every 'when' units, where units is one of
 * NOTE_SECONDS, NOTE_MSECONDS, NOTE_USECONDS, or NOTE_NSECONDS.  unit can also
 * be NOTE_ABSTIME, in which case the timer will be cancelled when it fires
 * regardless of the disposition return.
 */

typedef kqdisp (*kqtimercb) (kq_t *, void *udata);
int		kqtimer		(kq_t *, int when, unsigned unit,
				 kqtimercb handler, void *udata);

#endif	/* !NETD_KQ_H_INCLUDED */
