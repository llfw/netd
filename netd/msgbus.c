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

#include	<sys/queue.h>

#include	<stdlib.h>
#include	<stdint.h>

#include	"msgbus.h"
#include	"netd.h"

/* 
 * minimal implementation to make the API work.  this could obviously be much
 * more efficient.
 */

typedef struct subscriber {
	SLIST_ENTRY(subscriber)	su_entry;
	msgbus_handler_t	su_handler;
	msg_id_t		su_event;
} subscriber_t;

static SLIST_HEAD(subscriber_head, subscriber) subscribers
	= SLIST_HEAD_INITIALIZER(subscribers);

int
msgbus_init(void) {
	return 0;
}

void
msgbus_post(msg_id_t msgid, void *udata) {
subscriber_t	*sub;
	SLIST_FOREACH(sub, &subscribers, su_entry) {
		if (sub->su_event != msgid)
			continue;

		sub->su_handler(msgid, udata);
	}
}

void
msgbus_sub(msg_id_t msg, msgbus_handler_t handler) {
subscriber_t	*sub;

	if ((sub = calloc(1, sizeof(*sub))) == NULL)
		panic("msgbus_sub: out of memory");

	sub->su_event = msg;
	sub->su_handler = handler;
	SLIST_INSERT_HEAD(&subscribers, sub, su_entry);
}
