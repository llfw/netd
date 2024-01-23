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

#ifndef	NETD_MSGBUS_H_INCLUDED
#define	NETD_MSGBUS_H_INCLUDED

/*
 * msgbus is an internal message bus used to distribute events.
 */

#include	<functional>
#include	<set>

namespace msgbus {

template<typename... Args>
struct event final {
	event() = default;

	event(event const &) = delete;
	event(event &&) = delete;
	auto operator=(event const &) -> event& = delete;
	auto operator=(event &&) -> event& = delete;

	using handler = std::function<void (Args...)>;

	auto dispatch(Args... args) {
		for (auto &&handler: _handlers)
			(*handler)(std::forward<Args>(args)...);
	}

private:
	friend struct sub;
	std::set<handler *> _handlers;
};

struct sub final {
	sub() = default;

	sub(sub &&) = default;
	auto operator=(sub &&) -> sub& = default;

	sub(sub const &) = delete;
	auto operator=(sub const &) -> sub& = delete;

	template<typename... Args>
	sub(event<Args...> &ev, event<Args...>::handler handler)
	: _impl(new impl<Args...>(ev, std::move(handler))) {}

	struct impl_base {
		impl_base(impl_base const &) = delete;
		impl_base(impl_base &&) = delete;
		auto operator=(impl_base const &) -> impl_base& = delete;
		auto operator=(impl_base const &&) -> impl_base& = delete;

		virtual ~impl_base() {}

	protected:
		impl_base() = default;
	};

	template<typename... Args>
	struct impl final : impl_base {
		impl(event<Args...> &ev,
		     event<Args...>::handler handler)
		: _ev(ev)
		, _handler(std::move(handler)) {
			_ev._handlers.insert(&_handler);
		}

		impl(impl const &) = delete;
		impl(impl &&) = delete;
		auto operator=(impl const &) -> impl& = delete;
		auto operator=(impl const &&) -> impl& = delete;

		~impl() {
			auto it = _ev._handlers.find(&_handler);
			_ev._handlers.erase(it);
		}

	private:
		event<Args...>& _ev;
		event<Args...>::handler _handler;
	};

private:
	std::unique_ptr<impl_base> _impl;
};

/* initialise the msgbus */
auto init(void) -> int;

} // namespace msgbus

#endif	/* !NETD_MSGBUS_H_INCLUDED */