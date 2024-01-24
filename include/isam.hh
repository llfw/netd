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

#ifndef	NETD_ISAM_HH_INCLUDED
#define	NETD_ISAM_HH_INCLUDED

#include	<list>
#include	<vector>
#include	<functional>
#include	<algorithm>
#include	<ranges>
#include	<unordered_map>
#include	<cassert>

/*
 * a very simple in-memory ISAM-style container.  isam<T> is an std::list<T>
 * with one or more optional indices which can be used to look up objects
 * quickly.
 *
 * loosely inspired by (although entirely unrelated to) Boost's Multi Index
 * Container.
 *
 * TODO: flesh this out to be more like an actual container.
 */

namespace netd::isam {

template<typename T, typename K>
using extractor = std::function<K (T const &)>;

template<typename T> struct isam;

template<typename T>
struct index_base {
	virtual ~index_base() {}

protected:
	template<typename T1> friend struct isam;
	using list_iter = typename std::list<T>::iterator;

	isam<T> *_isam;

	index_base(struct isam<T> *isam)
	: _isam(isam) {
	}

	virtual auto insert(list_iter) -> void = 0;
	virtual auto erase(T const &) -> void = 0;
};

template<typename T, typename K> struct index;

template<typename T>
struct isam final {
	using iterator = std::list<T>::iterator;
	using const_iterator = std::list<T>::const_iterator;
	using reverse_iterator = std::list<T>::reverse_iterator;
	using const_reverse_iterator = std::list<T>::reverse_iterator;
	using value_type = std::list<T>::value_type;
	using reference = std::list<T>::reference;
	using const_reference = std::list<T>::const_reference;
	using size_type = std::list<T>::size_type;
	using pointer = std::list<T>::pointer;
	using const_pointer = std::list<T>::const_pointer;

	isam() = default;
	isam(isam const &) = delete;
	isam(isam &&) = delete;
	auto operator=(isam const &) = delete;
	auto operator=(isam &&) = delete;

	~isam() {
		for (auto idx: _indices)
			idx->_isam = nullptr;
	}

	auto insert(T const &v) -> iterator {
		insert(_list.end(), v);
	}

	auto insert(iterator where, T &&v) -> iterator {
		auto it = _list.insert(where, std::move(v));
		for (auto idx : _indices)
			idx->insert(it);
		return it;
	}

	auto erase(iterator item) -> void {
		for (auto idx : _indices)
			idx->erase(*item);
		_list.erase(item);
	}

	auto begin() {
		return _list.begin();
	}

	auto begin() const {
		return _list.begin();
	}

	auto end() {
		return _list.end();
	}

	auto end() const {
		return _list.end();
	}

	auto rbegin() {
		return _list.rbegin();
	}

	auto rbegin() const {
		return _list.rbegin();
	}

	auto rend() {
		return _list.rend();
	}

	auto rend() const {
		return _list.rend();
	}

private:
	std::list<T> _list;
	std::vector<index_base<T> *> _indices;
	template<typename T1, typename K> friend struct index;
};

template<typename T, typename K>
struct index final : index_base<T> {
private:
	friend struct isam<T>;
	using typename index_base<T>::list_iter;
	using map_type = std::map<K, list_iter>;

public:
	index(isam<T> &isam, extractor<T, K> ext)
	: index_base<T>(&isam), _ext(std::move(ext)) {
		this->_isam->_indices.push_back(this);
	}

	~index() {
		if (!this->_isam)
			return;

		auto it = std::ranges::find(this->_isam->_indices, this);
		assert(it != this->_isam->_indices.end());
		this->_isam->_indices.erase(it);
	}

	auto find(K const &key) {
		return _map.find(key);
	}

	auto find(K const &key) const {
		return _map.find(key);
	}

	auto begin() {
		return _map.begin();
	}

	auto begin() const {
		return _map.begin();
	}

	auto end() {
		return _map.end();
	}

	auto end() const {
		return _map.end();
	}

	auto rbegin() {
		return _map.rbegin();
	}

	auto rbegin() const {
		return _map.rbegin();
	}

	auto rend() {
		return _map.rend();
	}

	auto rend() const {
		return _map.rend();
	}

private:
	extractor<T, K> _ext;
	std::unordered_map<K, list_iter> _map;

	auto insert(list_iter it) -> void {
		auto key = _ext(*it);
		_map.insert({key, it});
	}

	auto erase(T const &item) -> void {
		auto key = _ext(item);
		_map.erase(_map.find(key));
	}

};

} // namespace netd::isam

#endif	/* !NETD_ISAM_HH_INCLUDED */
