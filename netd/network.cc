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
#include	<errno.h>
#include	<stdlib.h>
#include	<string.h>

#include	<new>

#include	"netd.hh"
#include	"network.hh"

namespace netd::network {

std::map<std::string_view, network *> networks;

auto find(std::string_view name) -> network * {
	if (auto it = networks.find(name); it != networks.end())
		return it->second;

	errno = ESRCH;
	return NULL;
}

auto create(std::string_view name)
	-> std::expected<network *, std::error_code>
{
struct network	*net;

	if (find(name) != NULL)
		return std::unexpected(
			std::make_error_code(std::errc(EEXIST)));

	if ((net = new (std::nothrow) network(name)) == NULL)
		return std::unexpected(
			std::make_error_code(std::errc(ENOMEM)));

	networks.emplace(net->name(), net);
	return net;
}

auto remove(network *nonnull net) -> void {
	assert(net);

	for (auto &&it : networks) {
		if (it.second != net)
			continue;

		networks.erase(it.first);
		return;
	}

	panic("network::remove: trying to remove non-existing network");
}

} // namespace netd::network
