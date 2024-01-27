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

#include <sys/uuid.h>

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <expected>
#include <list>
#include <memory>
#include <new>
#include <ranges>
#include <system_error>
#include <unordered_map>

#include "generator.hh"

import netd.util;
import log;

module network;

namespace netd::network {

struct network {
	network(std::string name, uuid id) noexcept
	try : _name(std::move(name)), _id(id) {
	} catch (std::bad_alloc const &) {
		panic("out of memory");
	} catch (...) {
		std::abort();
	}

	network(network const &) = delete;
	network(network &&) = delete;
	~network() noexcept = default;

	auto operator=(network const &) -> network & = delete;
	auto operator=(network &&) -> network & = delete;

	std::string _name;
	uuid	    _id;
};

isam::isam<network> networks;

isam::index networks_byname(networks,
			    [](network const &net) -> std::string_view {
				    return net._name;
			    });

isam::index networks_byid(networks, [](network const &net) { return net._id; });

uint64_t generation = 0;

auto add_network(std::string_view name, uuid id) -> network &
{
	auto it = networks.emplace(networks.end(), std::string(name), id);
	// we don't need to increment generation here because adding a new
	// network doesn't invalidate handles
	return *it;
}

auto getbyhandle(handle const &h) -> network &
{
	if (h.nh_gen == generation)
		return *h.nh_ptr;

	if (auto net = networks_byid.find(h.nh_uuid);
	    net != networks_byid.end()) {
		h.nh_gen = generation;
		return *h.nh_ptr;
	}

	panic("network: bad handle");
}

auto remove_byid(uuid id) -> bool
{
	if (auto it = networks_byid.find(id); it != networks_byid.end()) {
		auto lit = it->second;
		networks.erase(lit);
		++generation;
		return true;
	}

	return false;
}

auto make_handle(network &net) -> handle
{
	auto h = handle();
	h.nh_ptr = &net;
	h.nh_uuid = net._id;
	h.nh_gen = generation;
	return h;
}

auto find(std::string_view name) -> std::expected<handle, std::error_code>
{
	if (auto it = networks_byname.find(name); it != networks_byname.end())
		return make_handle(*it->second);
	return std::unexpected(error::from_errno(ESRCH));
}

auto findall() -> std::generator<handle>
{
	for (auto &&net: networks)
		co_yield make_handle(net);
}

auto info(handle const &network) -> std::expected<netinfo, std::error_code>
{
	auto &net = getbyhandle(network);
	auto  ret = netinfo{};
	ret.id = net._id;
	ret.name = net._name;
	return ret;
}

auto create(std::string_view name) -> std::expected<handle, std::error_code>
{
	if (find(name))
		return std::unexpected(error::from_errno(EEXIST));

	uuid id{};
	if (uuidgen(&id, 1) == -1)
		panic("network: uuidgen: %s", error::strerror());

	auto &net = add_network(name, id);
	return make_handle(net);
}

auto remove(handle const &network) -> void
{
	auto &net = getbyhandle(network);
	if (!remove_byid(net._id))
		panic("network::remove: trying to remove"
		      " non-existing network");
}

} // namespace netd::network
