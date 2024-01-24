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

#include	<cerrno>
#include	<cstdarg>
#include	<cstdio>
#include	<cstdlib>
#include	<cstring>
#include	<ctime>
#include	<print>

#include	"netd.hh"
#include	"kq.hh"
#include	"netlink.hh"
#include	"log.hh"
#include	"ctl.hh"
#include	"msgbus.hh"
#include	"iface.hh"
#include	"network.hh"

namespace netd {

auto _panic(std::string_view msg) -> void {
	std::print(stderr, "panic: {}\n", msg);
	std::fflush(stderr);
	abort();
}

} // namespace netd

int
main(int argc, char **argv) {
	using namespace netd;

	time(&kq::current_time);

	if (argc != 1) {
		std::print(stderr, "usage: {}\n", argv[0]);
		return 1;
	}

	log::info("starting");

	if (kq::init() == -1) {
		log::fatal("kqinit: {}", strerror(errno));
		return 1;
	}

	if (msgbus::init() == -1) {
		log::fatal("msgbus init failed: {}", strerror(errno));
		return 1;
	}

	/*
	 * iface has to be initialised before netlink so it can receive
	 * netlink's boot-time newlink/newaddr messages.
	 */
	if (iface::init() == -1) {
		log::fatal("iface init failed: {}", strerror(errno));
		return 1;
	}

	if (auto ret = netlink::init(); !ret) {
		log::fatal("netlink init failed: {}", ret.error().message());
		return 1;
	}

	if (auto ret = ctl::init(); !ret) {
		log::fatal("ctl init failed: {}", ret.error().message());
		return 1;
	}

	if (auto ret = kq::run(); !ret) {
		log::fatal("kqrun: {}", ret.error().message());
		return 1;
	}

	return 0;
}
