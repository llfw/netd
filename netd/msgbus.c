/*
 * This source code is released into the public domain.
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
