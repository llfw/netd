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
	time(&current_time);

	if (argc != 1) {
		fprintf(stderr, "usage: %s\n", argv[0]);
		return 1;
	}

	nlog(NLOG_INFO, "starting");

	if (kqinit() == -1) {
		nlog(NLOG_FATAL, "kqinit: %s", strerror(errno));
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

	if (nl_setup() == -1) {
		nlog(NLOG_FATAL, "netlink setup failed");
		return 1;
	}

	if (ctl_setup() == -1) {
		nlog(NLOG_FATAL, "ctl setup failed");
		return 1;
	}

	if (kqrun() == -1) {
		nlog(NLOG_FATAL, "kqrun: %s", strerror(errno));
		return 1;
	}

	return 0;
}

