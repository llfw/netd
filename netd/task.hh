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

#ifndef	NETD_TASK_HH_INCLUDED
#define	NETD_TASK_HH_INCLUDED

/*
 * a task is an awaitable object representing a suspendable coroutine.
 */

#include	<coroutine>

#include	"log.hh"

namespace netd {

namespace detail {

template <typename T>
struct promise_base {
	void return_value(T const &value) {
		result = value;
	}

	void return_value(T &&value) {
		result = std::move(value);
	}

	auto await_resume() -> T {
		return std::move(result);
	}

	T result{};
};

template <>
struct promise_base<void> {
	void return_void() {}
	auto await_resume() -> void {}
};

} // namespace detail

template <typename T>
struct task {
	struct promise_type : detail::promise_base<T> {
		std::coroutine_handle<> previous{};
		//std::exception_ptr exception{};
		//executor *task_executor{};

		auto get_return_object() {
			return task(
				std::coroutine_handle<
					promise_type
				>::from_promise(*this));
		}

		auto initial_suspend() noexcept -> std::suspend_always {
			return {};
		}

		struct final_awaiter {
			auto await_ready() noexcept -> bool {
				return false;
			}

			void await_resume() noexcept {}

			auto await_suspend(
				std::coroutine_handle<promise_type> h)
				-> std::coroutine_handle<>
			{
				auto &prev = h.promise().previous;
				if (prev)
					return prev;

				return std::noop_coroutine();
			}
		};

		auto final_suspend() noexcept -> final_awaiter {
			return {};
		}

		void unhandled_exception() noexcept {
			abort();
		}
	};

	std::coroutine_handle<promise_type> coro_handle;

	explicit task(std::coroutine_handle<promise_type> coro_handle_)
	: coro_handle(coro_handle_) {
		log::debug("task@{}: created", static_cast<void *>(this));
	}

	task(task const &) = delete;
	auto operator=(task const &) -> task & = delete;
	auto operator=(task &&other) -> task & = delete;

	task(task &&other)
	: coro_handle(std::exchange(other.coro_handle, {}))
	{
		log::debug("task@{}: moved", static_cast<void *>(this));
	}

	~task() {
		if (coro_handle) {
			log::debug("task@{}: destroyed with handle",
				   static_cast<void *>(this));
			coro_handle.destroy();
		} else {
			log::debug("task@{}: destroyed without handle",
				   static_cast<void *>(this));
		}
	}

	auto await_ready() -> bool {
		return false;
	}

	template <typename P>
	auto await_suspend(std::coroutine_handle<P> h) {
		log::debug("task@{}: await_suspend", static_cast<void *>(this));
		auto &promise = coro_handle.promise();
		promise.previous = h;
		//if (!promise.task_executor)
		//	promise.task_executor = h.promise().task_executor;
		return coro_handle;
	}

	auto await_resume() -> T {
		log::debug("task@{}: await_resume", static_cast<void *>(this));
		return coro_handle.promise().await_resume();
	}

	void start() {
		coro_handle.resume();
	}
};

#if 0
struct co_get_executor {
	executor *task_executor = nullptr;

	auto await_ready() -> bool {
		return false;
	}

	template <typename P>
	auto await_suspend(std::coroutine_handle<P> h) -> bool {
		task_executor = h.promise().task_executor;
		return false;
	}

	auto await_resume() -> executor * {
		return task_executor;
	}
};
#endif

} // namespace netd

#endif	/* !NETD_TASK_HH_INCLUDED */
