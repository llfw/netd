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

#ifndef	NETD_XO_HH_INCLUDED
#define	NETD_XO_HH_INCLUDED

/*
 * helpers for using libxo from C++.
 */

#include	<tuple>

extern "C" { // TODO: file upstream bug
#include	<libxo/xo.h>
}

namespace xo {

/*
 * xo calls xo_finish() when it's destroyed.
 */
struct xo {
	~xo() {
		xo_finish();
	}
};

/*
 * container opens a container of the given name and closes it when
 * it's destroyed.
 */
struct container {
	container(std::string name)
	: _name(std::move(name)) {
		xo_open_container(_name.c_str());
	}

	~container() {
		xo_close_container(_name.c_str());
	}

	container(container const &) = delete;
	container(container &&) = delete;
	auto operator=(container const &) -> container& = delete;
	auto operator=(container &&) -> container& = delete;

private:
	std::string _name;
};

/*
 * instance opens an instance of the given name and closes it when it's
 * destroyed.
 */
struct instance {
	instance(std::string name)
	: _name(std::move(name)) {
		xo_open_instance(_name.c_str());
	}

	~instance() {
		xo_close_instance(_name.c_str());
	}

	instance(instance const &) = delete;
	instance(instance &&) = delete;
	auto operator=(instance const &) -> instance& = delete;
	auto operator=(instance &&) -> instance& = delete;

private:
	std::string _name;
};

/*
 * emit() calls xo_emit(), but string-like objects are converted to C strings.
 *
 * this means you can do this:
 *
 * auto value = std::string("value");
 * xo::emit("{V:label/%s}", value);
 */

namespace detail {

	// remove the first element of a tuple
	template <typename T, typename... Rest>
	auto drop1(std::tuple<T, Rest...> const &tuple)
	{
	    return std::apply([](auto &&, auto const &... args) {
		    return std::tie(args...);
	    }, tuple);
	}

	/*
	 * the idea here is that emit() is first called with a tuple of raw
	 * arguments passed to xo::emit(), and an empty variadic argument list.
	 * each iteration removes one item from the tuple and puts the
	 * converted value at the end of the argument list.  eventually, the
	 * version with an empty tuple<> is called, which forwards all the
	 * converted arguments to xo_emit().
	 */

	template<typename... Cs>
	void emit(std::string_view format,
		  std::tuple<>,
		  Cs&&... converted) {
		xo_emit(std::string(format).c_str(),
			std::forward<Cs>(converted)...);
	}

	template<typename T, typename... Us, typename... Cs>
	requires std::same_as<std::string_view, std::remove_cvref_t<T>>
	void emit(std::string_view, std::tuple<T, Us...> const &, Cs&&...);

	template<typename T, typename... Us, typename... Cs>
	requires std::same_as<std::string, std::remove_cvref_t<T>>
	void emit(std::string_view, std::tuple<T, Us...> const &, Cs&&...);

	template<typename T, typename... Us, typename... Cs>
	requires (std::is_integral_v<std::remove_cvref_t<T>>
	          || std::is_pointer_v<std::remove_cvref_t<T>>)
	void emit(std::string_view, std::tuple<T, Us...> const &, Cs&&...);

	template<typename T, typename... Us, typename... Cs>
	requires std::same_as<std::string, std::remove_cvref_t<T>>
	void emit(std::string_view format,
		  std::tuple<T, Us...> const &unconverted,
		  Cs&&... done)
	{
		emit(format, drop1(unconverted), std::forward<Cs>(done)...,
		     std::get<0>(unconverted).c_str());
	}

	template<typename T, typename... Us, typename... Cs>
	requires std::same_as<std::string_view, std::remove_cvref_t<T>>
	void emit(std::string_view format,
		  std::tuple<T, Us...> const &unconverted,
		  Cs&&... done)
	{
		emit(format, drop1(unconverted), std::forward<Cs>(done)...,
		     std::string(std::get<0>(unconverted)).c_str());
	}

	template<typename T, typename... Us, typename... Cs>
	requires (std::is_integral_v<std::remove_cvref_t<T>>
	          || std::is_pointer_v<std::remove_cvref_t<T>>)
	void emit(std::string_view format,
		  std::tuple<T, Us...> const &unconverted,
		  Cs&&... done)
	{
		emit(format, drop1(unconverted), std::forward<Cs>(done)...,
		     std::get<0>(unconverted));
	}
}

template<typename... Args>
void emit(std::string_view format, Args&&... args) {
	detail::emit(format, std::tuple<Args...>(std::forward<Args>(args)...));

}

} // namespace xo

#endif	/* !NETD_XO_HH_INCLUDED */
