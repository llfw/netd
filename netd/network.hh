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

#include	<string>
#include	<map>
#include	<expected>
#include	<system_error>

#include	"defs.hh"

namespace netd::network {

/*
 * a network we know about, i.e. a layer 3 (IP) configuration which can be
 * applied to an interface.
 */

struct network {
	network(std::string_view name) : _name(name) {}

	network(network const &) = delete;
	network(network &&) = default;

	auto operator=(network const &) -> network& = delete;
	auto operator=(network &&) -> network& = default;

	auto name() const -> std::string const & {
		return _name;
	}

private:
	std::string	_name;
};

extern std::map<std::string_view, network *> networks;

/*
 * create a new network and add it to the configuration store.
 */
auto create(std::string_view name)
	-> std::expected<network *, std::error_code>;

/* retrieve an existing network; returns NULL if not found. */
auto find(std::string_view name) -> network *nullable;

/* delete a network.  all configuration will be removed. */
auto remove(network *nonnull) -> void;

} // namespace netd::network

#endif	/* !NETD_NETWORK_H_INCLUDED */
