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

#ifndef	NETD_LOG_H_INCLUDED
#define	NETD_LOG_H_INCLUDED

/*
 * message logging (syslog and stderr).
 */

#include	<string>
#include	<format>

#include	"defs.hh"

namespace netd::log {

enum class severity {
	debug,
	info,
	warning,
	error,
	fatal,
};

auto log_message(severity, std::string_view message) -> void;

namespace detail {
	template<severity sev>
	struct sev_log {
		template<typename... Args>
		auto operator()(std::format_string<Args...> fmt,
				Args&&... args) const -> void {
			auto msg = std::format(fmt,
					       std::forward<Args>(args)...);
			log_message(sev, msg);
		}
	};
}

constexpr auto fatal = detail::sev_log<severity::fatal>();
constexpr auto error = detail::sev_log<severity::error>();
constexpr auto warning = detail::sev_log<severity::warning>();
constexpr auto info = detail::sev_log<severity::info>();
constexpr auto debug = detail::sev_log<severity::debug>();

/* get or set the log destination */
constexpr auto syslog	= 0x1u;
constexpr auto console	= 0x2u;
constexpr auto destmask	= 0x3u;

constexpr auto defaultdest = console;

unsigned	getdest(void);
void		setdest(unsigned);

} // namespace netd::log

#endif	/* !NETD_LOG_H_INCLUDED */
