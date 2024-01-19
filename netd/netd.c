/*
 * This source code is released into the public domain.
 */

#include	<sys/types.h>
#include	<sys/event.h>

#include	<errno.h>
#include	<stdarg.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<time.h>

#include	"netd.h"
#include	"kq.h"
#include	"netlink.h"
#include	"log.h"
#include	"ctl.h"
#include	"msgbus.h"
#include	"state.h"

void
vpanic(char const *str, va_list args) {
	fputs("panic: ", stderr);
	vfprintf(stderr, str, args);
	fputs("\n", stderr);
	abort();
}

void
panic(char const *str, ...) {
va_list	ap;
	va_start(ap, str);
	vpanic(str, ap);
}

int
main(int argc, char **argv) {
struct kq	*kq;

	time(&current_time);

	if (argc != 1) {
		fprintf(stderr, "usage: %s\n", argv[0]);
		return 1;
	}

	nlog(NLOG_INFO, "starting");

	if ((kq = kq_create()) == NULL) {
		nlog(NLOG_FATAL, "kq_create: %s", strerror(errno));
		return 1;
	}

	if (msgbus_init() == -1) {
		nlog(NLOG_FATAL, "msgbus init failed");
		return 1;
	}

	/*
	 * state has to be initialised before netlink so it can receive
	 * netlink's boot-time newlink/newaddr messages.
	 */
	if (state_init() == -1) {
		nlog(NLOG_FATAL, "state init failed");
		return 1;
	}

	if (nl_setup(kq) == -1) {
		nlog(NLOG_FATAL, "netlink setup failed");
		return 1;
	}

	if (ctl_setup(kq) == -1) {
		nlog(NLOG_FATAL, "ctl setup failed");
		return 1;
	}

	if (kq_run(kq) == -1) {
		nlog(NLOG_FATAL, "kq_run: %s", strerror(errno));
		return 1;
	}

	return 0;
}

