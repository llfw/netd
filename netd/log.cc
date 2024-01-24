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

#include	<cassert>
#include	<cstdio>
#include	<cstdlib>
#include	<ctime>
#include	<print>

#include	<syslog.h>

#include	"netd.hh"

module log;

import kq;

namespace netd::log {

using namespace std::literals;

namespace {

unsigned logdest = defaultdest;

struct loglevel {
	std::string_view	ll_name;
	int			ll_syslog;
};

loglevel loglevels[] = {
	{ .ll_name = "debug"sv,   .ll_syslog = LOG_DEBUG },
	{ .ll_name = "info"sv,    .ll_syslog = LOG_INFO },
	{ .ll_name = "warning"sv, .ll_syslog = LOG_WARNING },
	{ .ll_name = "error"sv,   .ll_syslog = LOG_ERR },
	{ .ll_name = "fatal"sv,   .ll_syslog = LOG_CRIT },
};

auto log_console(loglevel level, std::string_view message) -> void {
struct tm	tm;
char		tmbuf[64] = {};

	localtime_r(&kq::current_time, &tm);
	strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%d %H:%M:%S %z", &tm);

	std::print(stderr, "{} [{}] {}\n",
		   tmbuf,
		   level.ll_name,
		   message);
}

auto log_syslog(loglevel  level, std::string_view message) -> void {
	::syslog(level.ll_syslog, "%s", std::string(message).c_str());
}

} // anonymous namespace

unsigned
getdest(void) {
	return logdest;
}

void
setdest(unsigned newdest) {
	assert((newdest & ~destmask) == 0u);

	if ((newdest & syslog) && !(logdest & syslog))
		openlog("dlctld", LOG_CONS | LOG_NDELAY | LOG_PID, LOG_DAEMON);
	else if (!(newdest & syslog) && (logdest & syslog))
		closelog();

	logdest = newdest;
}

auto log_message(severity sev, std::string_view message) -> void {
	auto &level = loglevels[static_cast<int>(sev)];

	if (logdest & syslog)
		log_syslog(level, message);

	if (logdest & console)
		log_console(level, message);
}

} // namespace netd::log
