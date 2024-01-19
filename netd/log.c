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

#include	<assert.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<syslog.h>
#include	<time.h>

#include	"log.h"
#include	"netd.h"
#include	"kq.h"

static unsigned logdest = NLOG_DEFAULT;

typedef struct {
	char const	*ll_name;
	int		 ll_syslog;
} leveldfn_t;

static leveldfn_t loglevels[] = {
	[NLOG_DEBUG]	= { .ll_name = "debug",   .ll_syslog = LOG_DEBUG },
	[NLOG_INFO]	= { .ll_name = "info",    .ll_syslog = LOG_INFO },
	[NLOG_WARNING]	= { .ll_name = "warning", .ll_syslog = LOG_WARNING },
	[NLOG_ERROR]	= { .ll_name = "error",   .ll_syslog = LOG_ERR },
	[NLOG_FATAL]	= { .ll_name = "fatal",   .ll_syslog = LOG_CRIT },
};

static void
log_stderr(loglevel_t level, char const *message) {
struct tm	tm;
char		tmbuf[64];
	assert(level >= 0 && level < ASIZE(loglevels));
	assert(message);

	localtime_r(&current_time, &tm);
	strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%d %H:%M:%S %z", &tm);

	fprintf(stderr, "%s [%s] %s\n", tmbuf, loglevels[level].ll_name,
		message);
}

static void
log_syslog(loglevel_t level, char const *message) {
	assert(level >= 0 && level < ASIZE(loglevels));
	assert(message);

	syslog(loglevels[level].ll_syslog, "%s", message);
}

unsigned
nlog_getdest(void) {
	return logdest;
}

void
nlog_setdest(unsigned newdest) {
	assert((newdest & ~NLOG_MASK) == 0u);

	if ((newdest & NLOG_SYSLOG) && !(logdest & NLOG_SYSLOG))
		openlog("dlctld", LOG_CONS | LOG_NDELAY | LOG_PID, LOG_DAEMON);
	else if (!(newdest & NLOG_SYSLOG) && (logdest & NLOG_SYSLOG))
		closelog();
	
	logdest = newdest;
}

void
nlog(loglevel_t level, char const *message, ...) {
va_list	args;

	assert(message);
	assert(level >= 0 && level < ASIZE(loglevels));

	va_start(args, message);
	vnlog(level, message, args);
	va_end(args);
}

void
vnlog(loglevel_t level, char const *message, va_list args) {
char	*buf = NULL;

	assert(message);
	assert(level >= 0 && level < ASIZE(loglevels));

	vasprintf(&buf, message, args);

	if (logdest & NLOG_SYSLOG)
		log_syslog(level, buf);
	if (logdest & NLOG_STDERR)
		log_stderr(level, buf);

	free(buf);
}
