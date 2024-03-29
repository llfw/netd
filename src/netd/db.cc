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

#include <expected>
#include <system_error>

#include "generator.hh"

module db;

import netd.network;
import netd.nvl;
import netd.util;

namespace netd::db {

auto save() noexcept -> std::expected<void, std::error_code>
{
	nvl state;

	auto serialise_network = [&](auto &hdl) {
		auto net = info(hdl);
		if (!net)
			panic("db::save: network::info: {}",
			      net.error().message());
		nvl data;

		data.add_string("name", net->name);
		return data;
	};

	for (auto &&hdl: network::findall())
		state.append_nvlist_array("networks", serialise_network(hdl));

	return {};
}

} // namespace netd::db
