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

#ifndef	NETD_NETWORK_H_INCLUDED
#define	NETD_NETWORK_H_INCLUDED

#include	<sys/uuid.h>

#include	<string>
#include	<map>
#include	<expected>
#include	<system_error>
#include	<cstdint>

#include	"defs.hh"
#include	"generator.hh"

namespace netd::network {

struct network;
/*
 * a handle representing a network.
 */
struct handle {
	network			*nh_ptr;
	uuid			 nh_uuid;
	mutable std::uint64_t	 nh_gen;
};

/*
 * retrieve a network's details.
 */

struct netinfo {
	uuid			id;
	std::string_view	name;
};

auto info(handle const &) -> std::expected<netinfo, std::error_code>;

/*
 * create a new network and add it to the configuration store.
 */
auto create(std::string_view name)
	-> std::expected<handle, std::error_code>;

/* retrieve an existing network; returns NULL if not found. */
auto find(std::string_view name) -> std::expected<handle, std::error_code>;

/* iterate all networks */
auto findall() -> std::generator<handle>;

/* delete a network.  all configuration will be removed. */
auto remove(handle const &) -> void;

} // namespace netd::network

#endif	/* !NETD_NETWORK_H_INCLUDED */
