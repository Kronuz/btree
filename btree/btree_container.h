/*
 * Copyright (c) 2019-2026 Germán Méndez Bravo (Kronuz)
 * Copyright (c) 2013 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Container base classes shared by btree::set, map, multiset and multimap.
 * A real header, but the pieces are meant to be used through btree/btree.h.
 */

// btree_container: the container base classes shared by set, map, multiset and multimap.

#ifndef BTREE_BTREE_CONTAINER_H__
#define BTREE_BTREE_CONTAINER_H__

#include <cstddef>
#include <initializer_list>
#include <ostream>
#include <type_traits>
#include <utility>

#include "btree_tree.h"

namespace btree {

// A common base class for btree::set, map, btree_multiset and
// btree_multimap.
template <typename Tree>
class btree_container {
	typedef btree_container<Tree> self_type;

public:
	typedef typename Tree::params_type params_type;
	typedef typename Tree::key_type key_type;
	typedef typename Tree::value_type value_type;
	typedef typename Tree::key_compare key_compare;
	typedef typename Tree::allocator_type allocator_type;
	typedef typename Tree::pointer pointer;
	typedef typename Tree::const_pointer const_pointer;
	typedef typename Tree::reference reference;
	typedef typename Tree::const_reference const_reference;
	typedef typename Tree::size_type size_type;
	typedef typename Tree::difference_type difference_type;
	typedef typename Tree::iterator iterator;
	typedef typename Tree::const_iterator const_iterator;
	typedef typename Tree::reverse_iterator reverse_iterator;
	typedef typename Tree::const_reverse_iterator const_reverse_iterator;

public:
	// Default constructor.
	btree_container(const key_compare& comp, const allocator_type& alloc)
		: __tree(comp, alloc) {
	}

	// Copy constructor.
	btree_container(const self_type& x)
		: __tree(x.__tree) {
	}

	btree_container(self_type&&) = default;
	self_type& operator=(const self_type&) = default;
	self_type& operator=(self_type&&) = default;

	// Iterator routines.
	iterator begin() {
		return __tree.begin();
	}
	const_iterator begin() const {
		return __tree.begin();
	}
	const_iterator cbegin() const {
		return __tree.cbegin();
	}
	iterator end() {
		return __tree.end();
	}
	const_iterator end() const {
		return __tree.end();
	}
	const_iterator cend() const {
		return __tree.cend();
	}
	reverse_iterator rbegin() {
		return __tree.rbegin();
	}
	const_reverse_iterator rbegin() const {
		return __tree.rbegin();
	}
	const_reverse_iterator crbegin() const {
		return __tree.crbegin();
	}
	reverse_iterator rend() {
		return __tree.rend();
	}
	const_reverse_iterator rend() const {
		return __tree.rend();
	}
	const_reverse_iterator crend() const {
		return __tree.crend();
	}

	// Lookup routines.
	iterator lower_bound(const key_type& key) {
		return __tree.lower_bound(key);
	}
	const_iterator lower_bound(const key_type& key) const {
		return __tree.lower_bound(key);
	}
	iterator upper_bound(const key_type& key) {
		return __tree.upper_bound(key);
	}
	const_iterator upper_bound(const key_type& key) const {
		return __tree.upper_bound(key);
	}
	std::pair<iterator,iterator> equal_range(const key_type& key) {
		return __tree.equal_range(key);
	}
	std::pair<const_iterator,const_iterator> equal_range(const key_type& key) const {
		return __tree.equal_range(key);
	}

	allocator_type get_allocator() const noexcept {
		return allocator_type(__tree.allocator());
	}

	// Utility routines.
	void clear() {
		__tree.clear();
	}
	void swap(self_type& x) {
		__tree.swap(x.__tree);
	}
	void dump(std::ostream& os) const {
		__tree.dump(os);
	}
	void verify() const {
		__tree.verify();
	}

	// Size routines.
	size_type size() const {
		return __tree.size();
	}
	size_type max_size() const {
		return __tree.max_size();
	}
	bool empty() const {
		return __tree.empty();
	}

	size_type height() const {
		return __tree.height();
	}
	size_type internal_nodes() const {
		return __tree.internal_nodes();
	}
	size_type leaf_nodes() const {
		return __tree.leaf_nodes();
	}
	size_type nodes() const {
		return __tree.nodes();
	}
	size_type bytes_used() const {
		return __tree.bytes_used();
	}
	static double average_bytes_per_value() {
		return Tree::average_bytes_per_value();
	}
	double fullness() const {
		return __tree.fullness();
	}
	double overhead() const {
		return __tree.overhead();
	}

	bool operator==(const self_type& x) const {
		if (size() != x.size()) {
			return false;
		}
		for (const_iterator i = begin(), xi = x.begin(); i != end(); ++i, ++xi) {
			if (*i !=* xi) {
				return false;
			}
		}
		return true;
	}

	bool operator!=(const self_type& other) const {
		return !operator==(other);
	}


 protected:
	Tree __tree;
};

template <typename T>
inline std::ostream& operator<<(std::ostream& os, const btree_container<T>& b) {
	b.dump(os);
	return os;
}

// A common base class for btree::map and btree::set.
template <typename Tree>
class btree_unique_container : public btree_container<Tree> {
	typedef btree_unique_container<Tree> self_type;
	typedef btree_container<Tree> super_type;

public:
	typedef typename Tree::key_type key_type;
	typedef typename Tree::value_type value_type;
	typedef typename Tree::size_type size_type;
	typedef typename Tree::key_compare key_compare;
	typedef typename Tree::allocator_type allocator_type;
	typedef typename Tree::iterator iterator;
	typedef typename Tree::const_iterator const_iterator;

public:
	// Default constructor.
	btree_unique_container(const key_compare& comp = key_compare(),
						   const allocator_type& alloc = allocator_type())
		: super_type(comp, alloc) {
	}

	// Copy constructor.
	btree_unique_container(const self_type& x)
		: super_type(x) {
	}

	btree_unique_container(self_type&&) = default;
	self_type& operator=(const self_type&) = default;
	self_type& operator=(self_type&&) = default;

	// Range constructor.
	template <class InputIterator>
	btree_unique_container(InputIterator b, InputIterator e,
						   const key_compare& comp = key_compare(),
						   const allocator_type& alloc = allocator_type())
		: super_type(comp, alloc) {
		insert(b, e);
	}

	// Lookup routines.
	iterator find(const key_type& key) {
		return this->__tree.find_unique(key);
	}
	const_iterator find(const key_type& key) const {
		return this->__tree.find_unique(key);
	}
	size_type count(const key_type& key) const {
		return this->__tree.count_unique(key);
	}
	#if __cplusplus > 201703L
		bool contains(const key_type& key) const {
			return find(key) != this->end();
		}
	#endif

	// Heterogeneous lookup (see btree_is_transparent): opt in via
	// std::less<> instead of the default std::less<key_type>.
	template <typename K, typename C = key_compare>
	std::enable_if_t<btree_is_transparent<C>::value, iterator> find(const K& key) {
		return this->__tree.find_unique(key);
	}
	template <typename K, typename C = key_compare>
	std::enable_if_t<btree_is_transparent<C>::value, const_iterator> find(const K& key) const {
		return this->__tree.find_unique(key);
	}
	template <typename K, typename C = key_compare>
	std::enable_if_t<btree_is_transparent<C>::value, size_type> count(const K& key) const {
		return this->__tree.count_unique(key);
	}
	#if __cplusplus > 201703L
		template <typename K, typename C = key_compare>
		std::enable_if_t<btree_is_transparent<C>::value, bool> contains(const K& key) const {
			return find(key) != this->end();
		}
	#endif

	// Insertion routines.
	std::pair<iterator, bool> insert(const value_type& x) {
		return this->__tree.insert_unique(x);
	}
	template <typename P>
	std::pair<iterator, bool> insert(P&& x) {
		return this->__tree.insert_unique(std::forward<P>(x));
	}
	std::pair<iterator, bool> insert(value_type&& x) {
		return this->__tree.insert_unique(std::move(x));
	}
	iterator insert(const_iterator hint, const value_type& x) {
		return this->__tree.insert_unique(hint, x);
	}
	template <typename P>
	std::pair<iterator, bool> insert(const_iterator hint, P&& x) {
		return this->__tree.insert_unique(hint, std::forward<P>(x));
	}
	iterator insert(const_iterator hint, value_type&& x) {
		return this->__tree.insert_unique(hint, std::move(x));
	}
	void insert(std::initializer_list<value_type> il) {
		insert(il.begin(), il.end());
	}
	template <typename InputIterator>
	void insert(InputIterator f, InputIterator l) {
		for (; f != l; ++f) {
			insert(this->cend(), *f);
		}
	}

	template <typename... Args>
	std::pair<iterator, bool> emplace(Args&&... args) {
		return this->__tree.emplace_unique(std::forward<Args>(args)...);
	}

	template <typename... Args>
	iterator emplace_hint(const_iterator hint, Args&&... args) {
		return this->__tree.emplace_hint_unique(hint, std::forward<Args>(args)...);
	}

	// Deletion routines.
	int erase(const key_type& key) {
		return this->__tree.erase_unique(key);
	}
	// Erase the specified iterator from the btree. The iterator must be valid
	// (i.e. not equal to end()).  Return an iterator pointing to the node after
	// the one that was erased (or end() if none exists).
	iterator erase(const iterator& iter) {
		return this->__tree.erase(iter);
	}
	void erase(const iterator& first, const iterator& last) {
		this->__tree.erase(first, last);
	}
};

// A common base class for btree::multimap and btree::multiset.
template <typename Tree>
class btree_multi_container : public btree_container<Tree> {
	typedef btree_multi_container<Tree> self_type;
	typedef btree_container<Tree> super_type;

 public:
	typedef typename Tree::key_type key_type;
	typedef typename Tree::value_type value_type;
	typedef typename Tree::size_type size_type;
	typedef typename Tree::key_compare key_compare;
	typedef typename Tree::allocator_type allocator_type;
	typedef typename Tree::iterator iterator;
	typedef typename Tree::const_iterator const_iterator;

 public:
	// Default constructor.
	btree_multi_container(const key_compare& comp = key_compare(),
						  const allocator_type& alloc = allocator_type())
		: super_type(comp, alloc) {
	}

	// Copy constructor.
	btree_multi_container(const self_type& x)
		: super_type(x) {
	}

	btree_multi_container(self_type&&) = default;
	self_type& operator=(const self_type&) = default;
	self_type& operator=(self_type&&) = default;

	// Range constructor.
	template <class InputIterator>
	btree_multi_container(InputIterator b, InputIterator e,
						  const key_compare& comp = key_compare(),
						  const allocator_type& alloc = allocator_type())
		: super_type(comp, alloc) {
		insert(b, e);
	}

	// Lookup routines.
	iterator find(const key_type& key) {
		return this->__tree.find_multi(key);
	}
	const_iterator find(const key_type& key) const {
		return this->__tree.find_multi(key);
	}
	size_type count(const key_type& key) const {
		return this->__tree.count_multi(key);
	}
	#if __cplusplus > 201703L
		bool contains(const key_type& key) const {
			return find(key) != this->end();
		}
	#endif

	// Insertion routines.
	iterator insert(const value_type& x) {
		return this->__tree.insert_multi(x);
	}
	template <typename P>
	iterator insert(P&& x) {
		return this->__tree.insert_multi(std::forward<P>(x));
	}
	iterator insert(value_type&& x) {
		return this->__tree.insert_multi(std::move(x));
	}
	iterator insert(const_iterator hint, const value_type& x) {
		return this->__tree.insert_multi(hint, x);
	}
	template <typename P>
	iterator insert(const_iterator hint, P&& x) {
		return this->__tree.insert_multi(hint, std::forward<P>(x));
	}
	iterator insert(const_iterator hint, value_type&& x) {
		return this->__tree.insert_multi(hint, std::move(x));
	}
	template <typename InputIterator>
	void insert(InputIterator f, InputIterator l) {
		for (; f != l; ++f) {
			insert(this->cend(), *f);
		}
	}
	void insert(std::initializer_list<value_type> il) {
		insert(il.begin(), il.end());
	}

	template <typename... Args>
	std::pair<iterator, bool> emplace(Args&&... args) {
		return this->__tree.emplace_multi(std::forward<Args>(args)...);
	}

	template <typename... Args>
	iterator emplace_hint(const_iterator hint, Args&&... args) {
		return this->__tree.emplace_hint_multi(hint, std::forward<Args>(args)...);
	}

	// Deletion routines.
	int erase(const key_type& key) {
		return this->__tree.erase_multi(key);
	}
	// Erase the specified iterator from the btree. The iterator must be valid
	// (i.e. not equal to end()).  Return an iterator pointing to the node after
	// the one that was erased (or end() if none exists).
	iterator erase(const iterator& iter) {
		return this->__tree.erase(iter);
	}
	void erase(const iterator& first, const iterator& last) {
		this->__tree.erase(first, last);
	}
};

} // namespace btree

#endif  // BTREE_BTREE_CONTAINER_H__
