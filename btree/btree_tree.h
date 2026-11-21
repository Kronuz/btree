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
 * The btree class: tree state, public operations, internals, and every member definition.
 * A real header, but the pieces are meant to be used through btree/btree.h.
 */

#ifndef BTREE_BTREE_TREE_H__
#define BTREE_BTREE_TREE_H__

#include <assert.h>
#include <algorithm>
#include <stddef.h>
#include <sys/types.h>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <ostream>
#include <type_traits>
#include <utility>
#include <vector>

#include "btree_compare.h"
#include "btree_params.h"
#include "btree_node.h"
#include "btree_iterator.h"

namespace btree {

// Dispatch helper class for using btree::internal_locate with plain compare.
struct btree_internal_locate_plain_compare {
	template <typename K, typename T, typename Iter>
	static std::pair<Iter, int> dispatch(const K& k, const T& t, Iter iter) {
		return t.internal_locate_plain_compare(k, iter);
	}
};

// Dispatch helper class for using btree::internal_locate with compare-to.
struct btree_internal_locate_compare_to {
	template <typename K, typename T, typename Iter>
	static std::pair<Iter, int> dispatch(const K& k, const T& t, Iter iter) {
		return t.internal_locate_compare_to(k, iter);
	}
};

template <typename Params>
class btree : public Params::key_compare {
	typedef btree<Params> self_type;
	typedef btree_node<Params> node_type;
	typedef typename node_type::base_fields base_fields;
	typedef typename node_type::leaf_fields leaf_fields;
	typedef typename node_type::internal_fields internal_fields;
	typedef typename node_type::root_fields root_fields;
	typedef typename Params::is_key_compare_to is_key_compare_to;

	friend struct btree_internal_locate_plain_compare;
	friend struct btree_internal_locate_compare_to;
	typedef std::conditional_t<is_key_compare_to::value,
		btree_internal_locate_compare_to,
		btree_internal_locate_plain_compare> internal_locate_type;

	enum {
		kNodeValues = node_type::kNodeValues,
		kMinNodeValues = kNodeValues / 2,
		kValueSize = node_type::kValueSize,
		kExactMatch = node_type::kExactMatch,
		kMatchMask = node_type::kMatchMask,
	};

	// A helper class to get the empty base class optimization for 0-size
	// allocators. Base is internal_allocator_type.
	// (e.g. empty_base_handle<internal_allocator_type, node_type*>). If Base is
	// 0-size, the compiler doesn't have to reserve any space for it and
	// sizeof(empty_base_handle) will simply be sizeof(Data). Google [empty base
	// class optimization] for more details.
	template <typename Base, typename Data>
	struct empty_base_handle : public Base {
		empty_base_handle(const Base& b, const Data& d)
			: Base(b),
			  data(d) {
		}
		Data data;
	};

	struct node_stats {
		node_stats(ssize_t l, ssize_t i)
			: leaf_nodes(l),
			  internal_nodes(i) {
		}

		node_stats& operator+=(const node_stats& x) {
			leaf_nodes += x.leaf_nodes;
			internal_nodes += x.internal_nodes;
			return *this;
		}

		ssize_t leaf_nodes;
		ssize_t internal_nodes;
	};

public:
	typedef Params params_type;
	typedef typename Params::key_type key_type;
	typedef typename Params::data_type data_type;
	typedef typename Params::mapped_type mapped_type;
	typedef typename Params::value_type value_type;
	typedef typename Params::mutable_value_type mutable_value_type;
	typedef typename Params::key_compare key_compare;
	typedef typename Params::pointer pointer;
	typedef typename Params::const_pointer const_pointer;
	typedef typename Params::reference reference;
	typedef typename Params::const_reference const_reference;
	typedef typename Params::size_type size_type;
	typedef typename Params::difference_type difference_type;
	typedef btree_iterator<node_type, reference, pointer> iterator;
	typedef typename iterator::const_iterator const_iterator;
	typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
	typedef std::reverse_iterator<iterator> reverse_iterator;

	typedef typename Params::allocator_type allocator_type;
	typedef typename Params::allocator_traits allocator_traits;
	typedef typename allocator_traits::template rebind_alloc<typename node_type::allocation_unit> internal_allocator_type;
	typedef std::allocator_traits<internal_allocator_type> internal_allocator_traits;

public:
	// Default constructor.
	btree(const key_compare& comp, const allocator_type& alloc);

	// Copy constructor.
	btree(const self_type& x);

	btree(self_type&& x) noexcept(std::is_nothrow_copy_constructible<key_compare>::value &&
		std::is_nothrow_copy_constructible<internal_allocator_type>::value)
		: key_compare(x.key_comp()), root_(x.internal_allocator(), nullptr) {
		__root() = x.__root();
		x.__root() = nullptr;
	}

	// Destructor.
	~btree() {
		clear();
	}

	// Iterator routines.
	iterator begin() {
		return iterator(leftmost(), 0);
	}
	const_iterator begin() const {
		return const_iterator(leftmost(), 0);
	}
	const_iterator cbegin() const {
		return const_iterator(leftmost(), 0);
	}
	iterator end() {
		return iterator(rightmost(), rightmost() ? rightmost()->count() : 0);
	}
	const_iterator end() const {
		return const_iterator(rightmost(), rightmost() ? rightmost()->count() : 0);
	}
	const_iterator cend() const {
		return const_iterator(rightmost(), rightmost() ? rightmost()->count() : 0);
	}
	reverse_iterator rbegin() {
		return reverse_iterator(end());
	}
	const_reverse_iterator rbegin() const {
		return const_reverse_iterator(end());
	}
	const_reverse_iterator crbegin() const {
		return const_reverse_iterator(end());
	}
	reverse_iterator rend() {
		return reverse_iterator(begin());
	}
	const_reverse_iterator rend() const {
		return const_reverse_iterator(begin());
	}
	const_reverse_iterator crend() const {
		return const_reverse_iterator(begin());
	}

	// Finds the first element whose key is not less than key.
	iterator lower_bound(const key_type& key) {
		return internal_end(internal_lower_bound(key, iterator(root(), 0)));
	}
	const_iterator lower_bound(const key_type& key) const {
		return internal_end(internal_lower_bound(key, const_iterator(root(), 0)));
	}

	// Finds the first element whose key is greater than key.
	iterator upper_bound(const key_type& key) {
		return internal_end(internal_upper_bound(key, iterator(root(), 0))); }
	const_iterator upper_bound(const key_type& key) const {
		return internal_end(internal_upper_bound(key, const_iterator(root(), 0))); }

	// Finds the range of values which compare equal to key. The first member of
	// the returned pair is equal to lower_bound(key). The second member pair of
	// the pair is equal to upper_bound(key).
	std::pair<iterator, iterator> equal_range(const key_type& key) {
		return std::make_pair(lower_bound(key), upper_bound(key));
	}
	std::pair<const_iterator, const_iterator> equal_range(const key_type& key) const {
		return std::make_pair(lower_bound(key), upper_bound(key));
	}

	// Inserts a value into the btree only if it does not already exist. The
	// boolean return value indicates whether insertion succeeded or failed.
	template <typename... Args>
	std::pair<iterator, bool> emplace_unique_key_args(const key_type& key, Args&&... args);

	template <typename P>
	std::pair<iterator, bool> emplace_unique(P&& x) {
		return emplace_unique_extract_key(std::forward<P>(x), btree_can_extract_key<P, key_type>());
	}

	template <typename First, typename Second>
	typename std::enable_if<
		btree_can_extract_map_key<First, key_type, value_type>::value,
		std::pair<iterator, bool>
	>::type emplace_unique(First&& f, Second&& s) {
		return emplace_unique_key_args(f,
			std::forward<First>(f),
			std::forward<Second>(s));
	}

	// Inserts a value into the btree only if it does not already exist. The
	// boolean return value indicates whether insertion succeeded or failed.
	template <typename... Args>
	std::pair<iterator, bool> emplace_unique(Args&&... args) {
		mutable_value_type v(std::forward<Args>(args)...);
		return emplace_unique_key_args(params_type::get_key(v), std::move(v));
	}

	template <class P>
	std::pair<iterator, bool>
	emplace_unique_extract_key(P&& x, btree_extract_key_fail_tag) {
		mutable_value_type v(std::forward<P>(x));
		return emplace_unique_key_args(params_type::get_key(v), std::move(v));
	}

	template <class P>
	std::pair<iterator, bool>
	emplace_unique_extract_key(P&& x, btree_extract_key_self_tag) {
		return emplace_unique_key_args(x, std::forward<P>(x));
	}

	template <class P>
	std::pair<iterator, bool>
	emplace_unique_extract_key(P&& x, btree_extract_key_first_tag) {
		return emplace_unique_key_args(x.first, std::forward<P>(x));
	}

	// Insert with hint. Check to see if the value should be placed immediately
	// before position in the tree. If it does, then the insertion will take
	// amortized constant time. If not, the insertion will take amortized
	// logarithmic time as if a call to emplace_unique(v) were made.
	template <typename... Args>
	iterator emplace_hint_unique_key_args(const_iterator hint, const key_type& key, Args&&... args);

	template <typename P>
	std::pair<iterator, bool> emplace_hint_unique(const_iterator hint, P&& x) {
		return emplace_hint_unique_extract_key(hint, std::forward<P>(x), btree_can_extract_key<P, key_type>());
	}

	template <typename First, typename Second>
	typename std::enable_if<
		btree_can_extract_map_key<First, key_type, value_type>::value,
		iterator
	>::type emplace_hint_unique(const_iterator hint, First&& f, Second&& s) {
		return emplace_hint_unique_key_args(hint, f,
			std::forward<First>(f),
			std::forward<Second>(s));
	}

	// Inserts a value into the btree only if it does not already exist. The
	// boolean return value indicates whether insertion succeeded or failed.
	template <typename... Args>
	iterator emplace_hint_unique(const_iterator hint, Args&&... args) {
		mutable_value_type v(std::forward<Args>(args)...);
		return emplace_hint_unique_key_args(hint, params_type::get_key(v), std::move(v));
	}

	template <class P>
	iterator
	emplace_hint_unique_extract_key(const_iterator hint, P&& x, btree_extract_key_fail_tag) {
		mutable_value_type v(std::forward<P>(x));
		return emplace_hint_unique_key_args(hint, params_type::get_key(v), std::move(v));
	}

	template <class P>
	iterator
	emplace_hint_unique_extract_key(const_iterator hint, P&& x, btree_extract_key_self_tag) {
		return emplace_hint_unique_key_args(hint, x, std::forward<P>(x));
	}

	template <class P>
	iterator
	emplace_hint_unique_extract_key(const_iterator hint, P&& x, btree_extract_key_first_tag) {
		return emplace_hint_unique_key_args(hint, x.first, std::forward<P>(x));
	}

	std::pair<iterator, bool> insert_unique(const value_type& v) {
		return emplace_unique_key_args(params_type::get_key(v), v);
	}
	std::pair<iterator, bool> insert_unique(value_type&& v) {
		return emplace_unique_key_args(params_type::get_key(v), std::move(v));
	}
	iterator insert_unique(const_iterator hint, const value_type& v) {
		return emplace_hint_unique_key_args(hint, params_type::get_key(v), v);
	}
	iterator insert_unique(const_iterator hint, value_type&& v) {
		return emplace_hint_unique_key_args(hint, params_type::get_key(v), std::move(v));
	}
	template <typename P, typename = typename std::enable_if<
		!std::is_same<
			typename std::remove_const<typename std::remove_reference<P>::type>::type,
			value_type
		>::value>::type>
	std::pair<iterator, bool> insert_unique(P&& x) {
		return emplace_unique(std::forward<P>(x));
	}
	template <typename P, typename = typename std::enable_if<
		!std::is_same<
			typename std::remove_const<typename std::remove_reference<P>::type>::type,
			value_type
		>::value>::type>
	iterator insert_unique(const_iterator hint, P&& x) {
		return emplace_hint_unique(hint, std::forward<P>(x));
	}

	// Inserts a value into the btree.
	template <typename... Args>
	iterator emplace_multi_key_args(const key_type& key, Args&&... args);

	// Inserts a value into the btree.
	template <typename... Args>
	iterator emplace_multi(Args&&... args) {
		mutable_value_type v(std::forward<Args>(args)...);
		return emplace_multi_key_args(params_type::get_key(v), std::move(v));
	}

	// Insert with hint. Check to see if the value should be placed immediately
	// before hint position in the tree. If it does, then the insertion will take
	// amortized constant time. If not, the insertion will take amortized
	// logarithmic time as if a call to emplace_multi(v) were made.
	template <typename... Args>
	iterator emplace_hint_multi_key_args(const_iterator hint, const key_type& key, Args&&... args);

	// Inserts a value into the btree.
	template <typename... Args>
	iterator emplace_hint_multi(const_iterator hint, Args&&... args) {
		mutable_value_type v(std::forward<Args>(args)...);
		return emplace_hint_multi_key_args(hint, params_type::get_key(v), std::move(v));
	}

	iterator insert_multi(const value_type& v) {
		return emplace_multi_key_args(params_type::get_key(v), v);
	}
	iterator insert_multi(value_type&& v) {
		return emplace_multi_key_args(params_type::get_key(v), std::move(v));
	}
	iterator insert_multi(const_iterator hint, const value_type& v) {
		return emplace_hint_multi_key_args(hint, params_type::get_key(v), v);
	}
	iterator insert_multi(const_iterator hint, value_type&& v) {
		return emplace_hint_multi_key_args(hint, params_type::get_key(v), std::move(v));
	}
	template <typename P, typename = typename std::enable_if<
		!std::is_same<
			typename std::remove_const<typename std::remove_reference<P>::type>::type,
			value_type
		>::value>::type>
	iterator insert_multi(P&& x) {
		return emplace_multi(std::forward<P>(x));
	}
	template <typename P, typename = typename std::enable_if<
		!std::is_same<
			typename std::remove_const<typename std::remove_reference<P>::type>::type,
			value_type
		>::value>::type>
	iterator insert_multi(const_iterator hint, P&& x) {
		return emplace_hint_multi(hint, std::forward<P>(x));
	}

	void assign(const self_type& x);

	// Erase the specified iterator from the btree. The iterator must be valid
	// (i.e. not equal to end()).  Return an iterator pointing to the node after
	// the one that was erased (or end() if none exists).
	iterator erase(const_iterator iter);

	// Erases range. Returns the number of keys erased.
	int erase(const_iterator begin, const_iterator end);

	// Erases the specified key from the btree. Returns 1 if an element was
	// erased and 0 otherwise.
	int erase_unique(const key_type& key);

	// Erases all of the entries matching the specified key from the
	// btree. Returns the number of elements erased.
	int erase_multi(const key_type& key);

	// Finds the iterator corresponding to a key or returns end() if the key is
	// not present.
	iterator find_unique(const key_type& key) {
		return internal_end(internal_find_unique(key, iterator(root(), 0)));
	}
	const_iterator find_unique(const key_type& key) const {
		return internal_end(internal_find_unique(key, const_iterator(root(), 0)));
	}
	iterator find_multi(const key_type& key) {
		return internal_end(internal_find_multi(key, iterator(root(), 0)));
	}
	const_iterator find_multi(const key_type& key) const {
		return internal_end(internal_find_multi(key, const_iterator(root(), 0)));
	}

	// Heterogeneous find/count (see btree_is_transparent), equality lookup
	// only -- not find_multi/lower_bound/upper_bound/equal_range.
	template <typename K, typename C = key_compare>
	std::enable_if_t<btree_is_transparent<C>::value, iterator> find_unique(const K& key) {
		return internal_end(internal_find_unique_hetero(key, iterator(root(), 0)));
	}
	template <typename K, typename C = key_compare>
	std::enable_if_t<btree_is_transparent<C>::value, const_iterator> find_unique(const K& key) const {
		return internal_end(internal_find_unique_hetero(key, const_iterator(root(), 0)));
	}

	// Returns a count of the number of times the key appears in the btree.
	size_type count_unique(const key_type& key) const {
		const_iterator begin = internal_find_unique(key, const_iterator(root(), 0));
		if (!begin.node) {
			// The key doesn't exist in the tree.
			return 0;
		}
		return 1;
	}
	template <typename K, typename C = key_compare>
	std::enable_if_t<btree_is_transparent<C>::value, size_type> count_unique(const K& key) const {
		return internal_find_unique_hetero(key, const_iterator(root(), 0)).node ? 1 : 0;
	}
	// Returns a count of the number of times the key appears in the btree.
	size_type count_multi(const key_type& key) const {
		return distance(lower_bound(key), upper_bound(key));
	}

	// Clear the btree, deleting all of the values it contains.
	void clear();

	// Swap the contents of* this and x.
	void swap(self_type& x);

	// Assign the contents of x to* this.
	self_type& operator=(const self_type& x) {
		if (std::addressof(x) == this) {
			// Don't copy onto ourselves.
			return *this;
		}
		assign(x);
		return *this;
	}

	self_type& operator=(self_type&& x) noexcept(std::is_nothrow_copy_assignable<key_compare>::value &&
		std::is_nothrow_copy_assignable<internal_allocator_type>::value) {
		if (std::addressof(x) != this) {
			clear();
			__key_comp() = x.key_comp();
			__internal_allocator() = x.internal_allocator();
			__root() = x.__root();
			x.__root() = nullptr;
		}
		return *this;
	}

	key_compare& __key_comp() {
		return *this;
	}
	const key_compare& key_comp() const {
		return *this;
	}
	bool compare_keys(const key_type& x, const key_type& y) const {
		return btree_compare_keys(key_comp(), x, y);
	}

	// Dump the btree to the specified ostream. Requires that operator<< is
	// defined for Key and Value.
	void dump(std::ostream& os) const {
		if (root() != nullptr) {
			internal_dump(os, root(), 0);
		}
	}

	// Verifies the structure of the btree.
	void verify() const;

	// Size routines. Note that empty() is slightly faster than doing size()==0.
	size_type size() const {
		if (empty()) return 0;
		if (root()->is_leaf()) return root()->count();
		return root()->size();
	}

	size_type max_size() const {
		return std::numeric_limits<size_type>::max();
	}

	bool empty() const {
		return root() == nullptr;
	}

	// The height of the btree. An empty tree will have height 0.
	size_type height() const {
		size_type h = 0;
		if (root()) {
			// Count the length of the chain from the leftmost node up to the
			// root. We actually count from the root back around to the level below
			// the root, but the calculation is the same because of the circularity
			// of that traversal.
			const node_type* n = root();
			do {
				++h;
				n = n->get_parent();
			} while (n != root());
		}
		return h;
	}

	// The number of internal, leaf and total nodes used by the btree.
	size_type leaf_nodes() const {
		return internal_stats(root()).leaf_nodes;
	}
	size_type internal_nodes() const {
		return internal_stats(root()).internal_nodes;
	}
	size_type nodes() const {
		node_stats stats = internal_stats(root());
		return stats.leaf_nodes + stats.internal_nodes;
	}

	// The total number of bytes used by the btree.
	size_type bytes_used() const {
		node_stats stats = internal_stats(root());
		if (!root()) return sizeof(*this);
		if (stats.leaf_nodes == 1 && stats.internal_nodes == 0) {
			return sizeof(*this) +
					node_type::allocation_size(node_type::leaf_size(root()->max_count()));
		} else {
			return sizeof(*this) +
					node_type::allocation_size(node_type::root_size()) - node_type::allocation_size(node_type::internal_size()) +
					stats.leaf_nodes * node_type::allocation_size(node_type::leaf_size(kNodeValues)) +
					stats.internal_nodes * node_type::allocation_size(node_type::internal_size());
		}
	}

	// The average number of bytes used per value stored in the btree.
	static double average_bytes_per_value() {
		// Returns the number of bytes per value on a leaf node that is 75%
		// full. Experimentally, this matches up nicely with the computed number of
		// bytes per value in trees that had their values inserted in random order.
		return node_type::allocation_size(node_type::leaf_size(kNodeValues)) / (kNodeValues * 0.75);
	}

	// The fullness of the btree. Computed as the number of elements in the btree
	// divided by the maximum number of elements a tree with the current number
	// of nodes could hold. A value of 1 indicates perfect space
	// utilization. Smaller values indicate space wastage.
	double fullness() const {
		return double(size()) / (nodes() * kNodeValues);
	}
	// The overhead of the btree structure in bytes per node. Computed as the
	// total number of bytes used by the btree minus the number of bytes used for
	// storing elements divided by the number of elements.
	double overhead() const {
		if (empty()) {
			return 0.0;
		}
		return (bytes_used() - size() * kValueSize) / double(size());
	}

private:
	// Internal accessor routines.
	node_type* root() {
		return root_.data;
	}
	const node_type* root() const {
		return root_.data;
	}
	node_type*& __root() {
		return root_.data;
	}

	// The rightmost node is stored in the root node.
	node_type* rightmost() {
		return (!root() || root()->is_leaf()) ? root() : root()->rightmost();
	}
	const node_type* rightmost() const {
		return (!root() || root()->is_leaf()) ? root() : root()->rightmost();
	}
	node_type*& __rightmost() {
		return root()->__rightmost();
	}

	// The leftmost node is stored as the parent of the root node.
	node_type* leftmost() {
		return root() ? root()->get_parent() : nullptr;
	}
	const node_type* leftmost() const {
		return root() ? root()->get_parent() : nullptr;
	}

	// The size of the tree is stored in the root node.
	size_type& __size() {
		return root()->__size();
	}

	// Allocator routines.
	internal_allocator_type& __internal_allocator() noexcept {
		return *static_cast<internal_allocator_type*>(&root_);
	}
	const internal_allocator_type& internal_allocator() const noexcept {
		return *static_cast<const internal_allocator_type*>(&root_);
	}

	allocator_type allocator() const noexcept {
		return allocator_type(internal_allocator());
	}

	// Node creation/deletion routines.
	void* allocate_node(std::size_t bytes) {
		static_assert(sizeof(node_type) == sizeof(base_fields), "node header contains trailing storage");
		auto& ia = __internal_allocator();
		auto count = node_type::allocation_count(bytes);
		auto storage = internal_allocator_traits::allocate(ia, count);
		constexpr auto alignment = alignof(value_type) > alignof(node_type) ? alignof(value_type) : alignof(node_type);
		if (reinterpret_cast<std::uintptr_t>(storage) % alignment) {
			internal_allocator_traits::deallocate(ia, storage, count);
			throw std::bad_alloc();
		}
		return storage;
	}
	void deallocate_node(node_type* node, std::size_t bytes) {
		node->destroy();
		std::destroy_at(node);
		internal_allocator_traits::deallocate(__internal_allocator(),
			reinterpret_cast<typename node_type::allocation_unit*>(node), node_type::allocation_count(bytes));
	}
	node_type* new_internal_node(node_type* parent) {
		return node_type::init_internal(allocate_node(node_type::internal_size()), parent);
	}
	node_type* new_internal_root_node() {
		return node_type::init_root(allocate_node(node_type::root_size()), root()->get_parent());
	}
	node_type* new_leaf_node(node_type* parent) {
		return node_type::init_leaf(allocate_node(node_type::leaf_size(kNodeValues)), parent, kNodeValues);
	}
	node_type* new_leaf_root_node(int max_count) {
		void* storage = allocate_node(node_type::leaf_size(max_count));
		return node_type::init_leaf(storage, static_cast<node_type*>(storage), max_count);
	}
	void delete_internal_node(node_type* node) {
		assert(node != root());
		deallocate_node(node, node_type::internal_size());
	}
	void delete_internal_root_node() {
		deallocate_node(root(), node_type::root_size());
	}
	void delete_leaf_node(node_type* node) {
		deallocate_node(node, node_type::leaf_size(node->max_count()));
	}

	// Rebalances or splits the node iter points to.
	void rebalance_or_split(iterator& iter);

	// Merges the values of left, right and the delimiting key on their parent
	// onto left, removing the delimiting key and deleting right.
	void merge_nodes(node_type* left, node_type* right);

	// Tries to merge node with its left or right sibling, and failing that,
	// rebalance with its left or right sibling. Returns true if a merge
	// occurred, at which point it is no longer valid to access node. Returns
	// false if no merging took place.
	bool try_merge_or_rebalance(const_iterator& iter);

	// Tries to shrink the height of the tree by 1.
	void try_shrink();

	// Merges or rebalances a leaf left underfull by a split whose insertion
	// then failed, walking up the tree as erase does.
	void repair_failed_insertion(const_iterator iter);

	struct copy_level {
		size_type nodes;
		size_type next = 0;
	};
	using copy_level_allocator = typename internal_allocator_traits::template rebind_alloc<copy_level>;
	using copy_plan = std::vector<copy_level, copy_level_allocator>;

	node_type* build_ordered_copy(const_iterator& source, copy_level* levels, std::size_t height,
		size_type leaf_values, node_type* parent, bool root_node) {
		const auto ordinal = levels[height].next++;
		int count;
		node_type* result;
		if (height == 0) {
			count = static_cast<int>(leaf_values / levels[0].nodes + (ordinal < leaf_values % levels[0].nodes));
			int capacity = kNodeValues;
			if (root_node) {
				capacity = 1;
				while (capacity < count) capacity = std::min<int>(kNodeValues, capacity * 2);
			}
			result = node_type::init_leaf(allocate_node(node_type::leaf_size(capacity)), parent, capacity);
		} else {
			const auto children = levels[height - 1].nodes;
			count = static_cast<int>(children / levels[height].nodes +
				(ordinal < children % levels[height].nodes)) - 1;
			result = root_node ? node_type::init_root(allocate_node(node_type::root_size()), parent)
				: new_internal_node(parent);
		}
		int children = 0;
		try {
			for (int i = 0; i <= count; ++i) {
				if (height != 0) {
					result->set_child(i, build_ordered_copy(source, levels, height - 1, leaf_values, result, false));
					++children;
				}
				if (i != count) {
					result->construct_value(i, *source);
					result->set_count(i + 1);
					++source;
				}
			}
		} catch (...) {
			for (int i = 0; i < children; ++i) internal_clear(result->child(i));
			if (result->is_leaf()) delete_leaf_node(result);
			else deallocate_node(result, root_node ? node_type::root_size() : node_type::internal_size());
			throw;
		}
		return result;
	}

	node_type* copy_root(const self_type& source) {
		const auto size = source.size();
		if (size == 0) return nullptr;
		const auto leaves = size / (kNodeValues + 1) + 1;
		auto iterator = source.begin();
		node_type* result;
		if (leaves == 1) {
			copy_level level{1};
			result = build_ordered_copy(iterator, &level, 0, size, nullptr, true);
		} else {
			copy_plan levels{copy_level_allocator(internal_allocator())};
			levels.push_back({leaves});
			while (levels.back().nodes > 1)
				levels.push_back({1 + (levels.back().nodes - 1) / (kNodeValues + 1)});
			result = build_ordered_copy(iterator, levels.data(), levels.size() - 1, size - leaves + 1, nullptr, true);
		}
		auto first = result;
		while (!first->is_leaf()) first = first->child(0);
		result->fields_.parent = first;
		if (!result->is_leaf()) {
			auto last = result;
			while (!last->is_leaf()) last = last->child(last->count());
			result->__rightmost() = last;
			result->__size() = size;
		}
		return result;
	}

	iterator internal_end(const_iterator iter) {
		return iter.node ? iterator(iter.node, iter.position) : end();
	}
	const_iterator internal_end(const_iterator iter) const {
		return iter.node ? iter : end();
	}

	// Inserts a value into the btree immediately before iter. Requires that
	// key(v) <= iter.key() and (--iter).key() <= key(v).
	template <typename... Args>
	iterator internal_emplace(const_iterator hint, Args&&... args);
	template <typename... Args>
	iterator emplace_empty(Args&&... args);
	template <typename... Args>
	iterator replace_leaf(node_type* original, int position, int capacity, Args&&... args);
	template <typename... Args>
	iterator emplace_nonfull(iterator iter, Args&&... args);

	// Returns an iterator pointing to the first value >= the value "iter" is
	// pointing at. Note that "iter" might be pointing to an invalid location as
	// iter.position == iter.node->count(). This routine simply moves iter up in
	// the tree to a valid location.
	template <typename IterType>
	static IterType internal_last(IterType iter);

	// Returns an iterator pointing to the leaf position at which key would
	// reside in the tree. We provide 2 versions of internal_locate. The first
	// version (internal_locate_plain_compare) always returns 0 for the second
	// field of the pair. The second version (internal_locate_compare_to) is for
	// the key-compare-to specialization and returns either kExactMatch (if the
	// key was found in the tree) or -kExactMatch (if it wasn't) in the second
	// field of the pair. The compare_to specialization allows the caller to
	// avoid a subsequent comparison to determine if an exact match was made,
	// speeding up string keys.
	template <typename IterType>
	std::pair<IterType, int> internal_locate(const key_type& key, IterType iter) const;
	template <typename IterType>
	std::pair<IterType, int> internal_locate_plain_compare(const key_type& key, IterType iter) const;
	template <typename IterType>
	std::pair<IterType, int> internal_locate_compare_to(const key_type& key, IterType iter) const;

	// Internal routine which implements lower_bound().
	template <typename IterType>
	IterType internal_lower_bound(const key_type& key, IterType iter) const;

	// Internal routine which implements upper_bound().
	template <typename IterType>
	IterType internal_upper_bound(const key_type& key, IterType iter) const;

	// Internal routine which implements find_unique().
	template <typename IterType>
	IterType internal_find_unique(const key_type& key, IterType iter) const;

	// Heterogeneous find_unique(): always the plain-compare loop.
	template <typename K, typename IterType>
	IterType internal_find_unique_hetero(const K& key, IterType iter) const {
		if (iter.node) {
			for (;;) {
				iter.position = iter.node->lower_bound(key, key_comp()) & kMatchMask;
				if (iter.node->is_leaf()) {
					break;
				}
				iter.node = iter.node->child(iter.position);
			}
			iter = internal_last(iter);
			if (iter.node && !key_comp()(key, iter.key())) {
				return iter;
			}
		}
		return IterType(nullptr, 0);
	}

	// Internal routine which implements find_multi().
	template <typename IterType>
	IterType internal_find_multi(const key_type& key, IterType iter) const;

	// Deletes a node and all of its children.
	void internal_clear(node_type* node);

	// Dumps a node and all of its children to the specified ostream.
	void internal_dump(std::ostream& os, const node_type* node, int level) const;

	// Verifies the tree structure of node.
	int internal_verify(const node_type* node, const key_type* lo, const key_type* hi) const;

	node_stats internal_stats(const node_type* node) const {
		if (!node) {
			return node_stats(0, 0);
		}
		if (node->is_leaf()) {
			return node_stats(1, 0);
		}
		node_stats res(0, 1);
		for (int i = 0; i <= node->count(); ++i) {
			res += internal_stats(node->child(i));
		}
		return res;
	}

private:
	empty_base_handle<internal_allocator_type, node_type*> root_;

private:
	// A never instantiated helper function that returns big_ if we have a
	// key-compare-to functor or if R is bool and small_ otherwise.
	template <typename R>
	static std::conditional_t<
		std::conditional_t<is_key_compare_to::value,
			std::is_same<R, int>,
			std::is_same<R, bool>>::value,
		big_,
		small_> key_compare_checker(R);

	// Verify that key_compare returns a bool. This is similar to the way
	// is_convertible in base/type_traits.h works. Note that key_compare_checker
	// is never actually invoked. The compiler will select which
	// key_compare_checker() to instantiate and then figure out the size of the
	// return type of key_compare_checker() at compile time which we then check
	// against the sizeof of big_.
	static_assert(sizeof(key_compare_checker(std::declval<const key_compare&>()(std::declval<const key_type&>(), std::declval<const key_type&>()))) == sizeof(big_),
		"key_comparison_function_must_return_bool");

	// Note: We insist on kTargetValues, which is computed from
	// Params::kTargetNodeSize, must fit the base_fields::field_type.
	static_assert((kNodeValues < (1 << (8 * sizeof(typename base_fields::field_type)))),
		"target_node_size_too_large");

	// Test the assumption made in setting kNodeValueSpace.
	static_assert(sizeof(base_fields) >= 2 * sizeof(void*),
		"node_space_assumption_incorrect");
};

////
// btree methods
template <typename P>
btree<P>::btree(const key_compare& comp, const allocator_type& alloc)
	: key_compare(comp),
	  root_(alloc, nullptr) {
}

template <typename P>
btree<P>::btree(const self_type& x)
	: key_compare(x.key_comp()),
	  root_(x.internal_allocator(), nullptr) {
	assign(x);
}

template <typename P> template <typename... Args>
std::pair<typename btree<P>::iterator, bool>
btree<P>::emplace_unique_key_args(const key_type& key, Args&&... args) {
	if (empty()) {
		return std::make_pair(emplace_empty(std::forward<Args>(args)...), true);
	}

	std::pair<iterator, int> res = internal_locate(key, iterator(root(), 0));
	iterator& iter = res.first;
	if (res.second == kExactMatch) {
		// The key already exists in the tree, do nothing.
		return std::make_pair(internal_last(iter), false);
	} else if (!res.second) {
		iterator last = internal_last(iter);
		if (last.node && !compare_keys(key, last.key())) {
			// The key already exists in the tree, do nothing.
			return std::make_pair(last, false);
		}
	}

	return std::make_pair(internal_emplace(iter, std::forward<Args>(args)...), true);
}

template <typename P> template <typename... Args>
inline typename btree<P>::iterator
btree<P>::emplace_hint_unique_key_args(const_iterator hint, const key_type& key, Args&&... args) {
	if (!empty()) {
		if (hint == end() || compare_keys(key, hint.key())) {
			const_iterator prev = hint;
			if (hint == begin() || compare_keys((--prev).key(), key)) {
				// prev.key() < key < hint.key()
				return internal_emplace(hint, std::forward<Args>(args)...);
			}
		} else if (compare_keys(hint.key(), key)) {
			const_iterator next = hint;
			++next;
			if (next == end() || compare_keys(key, next.key())) {
				// hint.key() < key < next.key()
				return internal_emplace(next, std::forward<Args>(args)...);
			}
		} else {
			// hint.key() == key
			return iterator(hint.node, hint.position);
		}
	}
	return emplace_unique(std::forward<Args>(args)...).first;
}

template <typename P> template <typename... Args>
typename btree<P>::iterator
btree<P>::emplace_multi_key_args(const key_type& key, Args&&... args) {
	if (empty()) {
		return emplace_empty(std::forward<Args>(args)...);
	}

	iterator iter = internal_upper_bound(key, iterator(root(), 0));
	if (!iter.node) {
		iter = end();
	}
	return internal_emplace(iter, std::forward<Args>(args)...);
}

template <typename P> template <typename... Args>
typename btree<P>::iterator
btree<P>::emplace_hint_multi_key_args(const_iterator hint, const key_type& key, Args&&... args) {
	if (!empty()) {
		if (hint == end() || !compare_keys(hint.key(), key)) {
			const_iterator prev = hint;
			if (hint == begin() || !compare_keys(key, (--prev).key())) {
				// prev.key() <= key <= hint.key()
				return internal_emplace(hint, std::forward<Args>(args)...);
			}
		} else {
			const_iterator next = hint;
			++next;
			if (next == end() || !compare_keys(next.key(), key)) {
				// hint.key() < key <= next.key()
				return internal_emplace(next, std::forward<Args>(args)...);
			}
		}
	}
	return emplace_multi(std::forward<Args>(args)...);
}

template <typename P>
void btree<P>::assign(const self_type& x) {
	clear();

	__key_comp() = x.key_comp();
	__internal_allocator() = x.internal_allocator();

	__root() = copy_root(x);
}

template <typename P>
typename btree<P>::iterator btree<P>::erase(const_iterator iter) {
	bool internal_delete = false;
	if (!iter.node->is_leaf()) {
		// Deletion of a value on an internal node. Swap the key with the largest
		// value of our left child. This is easy, we just decrement iter.
		const_iterator tmp_iter(iter--);
		assert(iter.node->is_leaf());
		assert(!compare_keys(tmp_iter.key(), iter.key()));
		iter.node->swap_value(iter.position, tmp_iter.node, tmp_iter.position);
		internal_delete = true;
		--__size();
	} else if (!root()->is_leaf()) {
		--__size();
	}

	// Delete the key from the leaf.
	iter.node->remove_value(iter.position);

	// We want to return the next value after the one we just erased. If we
	// erased from an internal node (internal_delete == true), then the next
	// value is ++(++iter). If we erased from a leaf node (internal_delete ==
	// false) then the next value is ++iter. Note that ++iter may point to an
	// internal node and the value in the internal node may move to a leaf node
	// (iter.node) when rebalancing is performed at the leaf level.

	// Merge/rebalance as we walk back up the tree.
	iterator res(iter.node, iter.position);
	for (;;) {
		if (iter.node == root()) {
			try_shrink();
			if (empty()) {
				return end();
			}
			break;
		}
		if (iter.node->count() >= kMinNodeValues) {
			break;
		}
		bool merged = try_merge_or_rebalance(iter);
		if (iter.node->is_leaf()) {
			res = iterator(iter.node, iter.position);
		}
		if (!merged) {
			break;
		}
		iter.node = iter.node->get_parent();
	}

	// Adjust our return value. If we're pointing at the end of a node, advance
	// the iterator.
	if (res.position == res.node->count()) {
		res.position = res.node->count() - 1;
		++res;
	}
	// If we erased from an internal node, advance the iterator.
	if (internal_delete) {
		++res;
	}
	return res;
}

template <typename P>
int btree<P>::erase(const_iterator begin, const_iterator end) {
	int count = distance(begin, end);
	for (int i = 0; i < count; i++) {
		begin = erase(begin);
	}
	return count;
}

template <typename P>
int btree<P>::erase_unique(const key_type& key) {
	iterator iter = internal_find_unique(key, iterator(root(), 0));
	if (!iter.node) {
		// The key doesn't exist in the tree, return nothing done.
		return 0;
	}
	erase(iter);
	return 1;
}

template <typename P>
int btree<P>::erase_multi(const key_type& key) {
	iterator begin = internal_lower_bound(key, iterator(root(), 0));
	if (!begin.node) {
		// The key doesn't exist in the tree, return nothing done.
		return 0;
	}
	// Delete all of the keys between begin and upper_bound(key).
	iterator end = internal_end(internal_upper_bound(key, iterator(root(), 0)));
	return erase(begin, end);
}

template <typename P>
void btree<P>::clear() {
	if (root() != nullptr) {
		internal_clear(root());
	}
	__root() = nullptr;
}

template <typename P>
void btree<P>::swap(self_type& x) {
	std::swap(static_cast<key_compare&>(*this), static_cast<key_compare&>(x));
	std::swap(root_, x.root_);
}

template <typename P>
void btree<P>::verify() const {
	if (root() != nullptr) {
		assert(size() == internal_verify(root(), nullptr, nullptr));
		assert(leftmost() == (++const_iterator(root(), -1)).node);
		assert(rightmost() == (--const_iterator(root(), root()->count())).node);
		assert(leftmost()->is_leaf());
		assert(rightmost()->is_leaf());
	} else {
		assert(size() == 0);
		assert(leftmost() == nullptr);
		assert(rightmost() == nullptr);
	}
}

template <typename P>
void btree<P>::rebalance_or_split(iterator& iter) {
	node_type*& node = iter.node;
	int& insert_position = iter.position;
	assert(node->count() == node->max_count());
	node_type* split_node = nullptr;

	// First try to make room on the node by rebalancing.
	node_type* parent = node->get_parent();
	if (node != root()) {
		if (node->position() > 0) {
			// Try rebalancing with our left sibling.
			node_type* left = parent->child(node->position() - 1);
			if (left->count() < left->max_count()) {
				// We bias rebalancing based on the position being inserted. If we're
				// inserting at the end of the right node then we bias rebalancing to
				// fill up the left node.
				int to_move = (left->max_count() - left->count()) /
						(1 + (insert_position < left->max_count()));
				to_move = std::max(1, to_move);

				if (((insert_position - to_move) >= 0) ||
						((left->count() + to_move) < left->max_count())) {
					left->rebalance_right_to_left(node, to_move);

					assert(node->max_count() - node->count() == to_move);
					insert_position = insert_position - to_move;
					if (insert_position < 0) {
						insert_position = insert_position + left->count() + 1;
						node = left;
					}

					assert(node->count() < node->max_count());
					return;
				}
			}
		}

		if (node->position() < parent->count()) {
			// Try rebalancing with our right sibling.
			node_type* right = parent->child(node->position() + 1);
			if (right->count() < right->max_count()) {
				// We bias rebalancing based on the position being inserted. If we're
				// inserting at the beginning of the left node then we bias rebalancing
				// to fill up the right node.
				int to_move = (right->max_count() - right->count()) /
						(1 + (insert_position > 0));
				to_move = std::max(1, to_move);

				if ((insert_position <= (node->count() - to_move)) ||
						((right->count() + to_move) < right->max_count())) {
					node->rebalance_left_to_right(right, to_move);

					if (insert_position > node->count()) {
						insert_position = insert_position - node->count() - 1;
						node = right;
					}

					assert(node->count() < node->max_count());
					return;
				}
			}
		}

		// Rebalancing failed, so this node splits. Allocate its sibling before
		// making room on the parent: allocations run from the leaf upward and
		// all precede the first value move, so a refusal anywhere in the chain
		// releases what was allocated and leaves the tree as it was.
		split_node = node->is_leaf() ? new_leaf_node(parent) : new_internal_node(parent);
		if (parent->count() == parent->max_count()) {
			iterator parent_iter(node->get_parent(), node->position());
			try {
				rebalance_or_split(parent_iter);
			} catch (...) {
				if (split_node->is_leaf()) delete_leaf_node(split_node);
				else delete_internal_node(split_node);
				throw;
			}
		}
	} else {
		// Rebalancing not possible because this is the root node. Allocate the
		// sibling and any new root before reshaping anything.
		split_node = node->is_leaf() ? new_leaf_node(parent) : new_internal_node(parent);
		try {
			if (root()->is_leaf()) {
				// The root node is currently a leaf node: create a new root node and set
				// the current root node as the child of the new root.
				parent = new_internal_root_node();
			} else {
				// The root node is an internal node. We do not want to create a new root
				// node because the root node is special and holds the size of the tree
				// and a pointer to the rightmost node. So we create a new internal node
				// and move all of the items on the current root into the new node.
				parent = new_internal_node(parent);
			}
		} catch (...) {
			if (split_node->is_leaf()) delete_leaf_node(split_node);
			else delete_internal_node(split_node);
			throw;
		}
		if (root()->is_leaf()) {
			parent->set_child(0, root());
			__root() = parent;
			assert(__rightmost() == parent->child(0));
		} else {
			parent->set_child(0, parent);
			parent->swap(root());
			node = parent;
		}
	}

	// Split the node.
	node->split(split_node, insert_position);
	if (node->is_leaf() && rightmost() == node) {
		__rightmost() = split_node;
	}

	if (insert_position > node->count()) {
		insert_position = insert_position - node->count() - 1;
		node = split_node;
	}
}

template <typename P>
void btree<P>::merge_nodes(node_type* left, node_type* right) {
	left->merge(right);
	if (right->is_leaf()) {
		if (rightmost() == right) {
			__rightmost() = left;
		}
		delete_leaf_node(right);
	} else {
		delete_internal_node(right);
	}
}

template <typename P>
bool btree<P>::try_merge_or_rebalance(const_iterator& iter) {
	node_type* parent = iter.node->get_parent();
	if (iter.node->position() > 0) {
		// Try merging with our left sibling.
		node_type* left = parent->child(iter.node->position() - 1);
		if ((1 + left->count() + iter.node->count()) <= left->max_count()) {
			iter.position += 1 + left->count();
			merge_nodes(left, iter.node);
			iter.node = left;
			return true;
		}
	}
	if (iter.node->position() < parent->count()) {
		// Try merging with our right sibling.
		node_type* right = parent->child(iter.node->position() + 1);
		if ((1 + iter.node->count() + right->count()) <= right->max_count()) {
			merge_nodes(iter.node, right);
			return true;
		}
		// Try rebalancing with our right sibling. We don't perform rebalancing if
		// we deleted the first element from iter.node and the node is not
		// empty. This is a small optimization for the common pattern of deleting
		// from the front of the tree.
		if ((right->count() > kMinNodeValues) && ((iter.node->count() == 0) || (iter.position > 0))) {
			int to_move = (right->count() - iter.node->count()) / 2;
			to_move = std::min(to_move, right->count() - 1);
			iter.node->rebalance_right_to_left(right, to_move);
			return false;
		}
	}
	if (iter.node->position() > 0) {
		// Try rebalancing with our left sibling. We don't perform rebalancing if
		// we deleted the last element from iter.node and the node is not
		// empty. This is a small optimization for the common pattern of deleting
		// from the back of the tree.
		node_type* left = parent->child(iter.node->position() - 1);
		if ((left->count() > kMinNodeValues) && ((iter.node->count() == 0) || (iter.position < iter.node->count()))) {
			int to_move = (left->count() - iter.node->count()) / 2;
			to_move = std::min(to_move, left->count() - 1);
			left->rebalance_left_to_right(iter.node, to_move);
			iter.position += to_move;
			return false;
		}
	}
	return false;
}

template <typename P>
void btree<P>::repair_failed_insertion(const_iterator iter) {
	while (iter.node != root() && iter.node->count() < kMinNodeValues) {
		if (!try_merge_or_rebalance(iter)) {
			break;
		}
		iter.node = iter.node->get_parent();
	}
	if (iter.node == root()) {
		try_shrink();
	}
}

template <typename P>
void btree<P>::try_shrink() {
	if (root()->count() > 0) {
		return;
	}
	// Deleted the last item on the root node, shrink the height of the tree.
	if (root()->is_leaf()) {
		assert(size() == 0);
		delete_leaf_node(root());
		__root() = nullptr;
	} else {
		node_type* child = root()->child(0);
		if (child->is_leaf()) {
			// The child is a leaf node so simply make it the root node in the tree.
			child->make_root();
			delete_internal_root_node();
			__root() = child;
		} else {
			// The child is an internal node. We want to keep the existing root node
			// so we move all of the values from the child node into the existing
			// (empty) root node.
			child->swap(root());
			delete_internal_node(child);
		}
	}
}

template <typename P> template <typename IterType>
inline IterType btree<P>::internal_last(IterType iter) {
	while (iter.node && iter.position == iter.node->count()) {
		iter.position = iter.node->position();
		iter.node = iter.node->get_parent();
		if (iter.node->is_leaf()) {
			iter.node = nullptr;
		}
	}
	return iter;
}

template <typename P> template <typename... Args>
inline typename btree<P>::iterator
btree<P>::emplace_empty(Args&&... args) {
	auto* node = new_leaf_root_node(1);
	try {
		node->insert_value(0, std::forward<Args>(args)...);
	} catch (...) {
		delete_leaf_node(node);
		throw;
	}
	__root() = node;
	return iterator(node, 0);
}

template <typename P> template <typename... Args>
inline typename btree<P>::iterator
btree<P>::replace_leaf(node_type* original, int position, int capacity, Args&&... args) {
	auto* replacement = original == root() ? new_leaf_root_node(capacity)
		: node_type::init_leaf(allocate_node(node_type::leaf_size(capacity)), original->get_parent(), capacity);
	bool incoming_live = false;
	bool keys_consumed = false;
	try {
		replacement->construct_value(position, std::forward<Args>(args)...);
		incoming_live = true;
		for (int i = 0, source = 0; i <= original->count(); ++i) {
			if (i == position) {
				incoming_live = false;
			} else if constexpr (std::is_nothrow_move_constructible<mutable_value_type>::value ||
					std::is_copy_constructible<mutable_value_type>::value) {
				replacement->construct_value(i, std::move_if_noexcept(original->mutable_value(source++)));
			} else if constexpr (!std::is_same<key_type, value_type>::value && std::is_copy_constructible<key_type>::value) {
				// A move-only mapped value whose move may throw: relocate through the
				// const view, which copies the key, so a failure keeps every key.
				replacement->construct_value(i, std::move(original->value(source++)));
			} else {
				// A throwing move with no copy fallback consumes keys.
				keys_consumed = true;
				replacement->construct_value(i, std::move(original->mutable_value(source++)));
			}
			replacement->set_count(i + 1);
		}
	} catch (...) {
		if (incoming_live) replacement->destroy_value(position);
		delete_leaf_node(replacement);
		if (keys_consumed) clear();
		throw;
	}
	if (original == root()) {
		__root() = replacement;
	} else {
		const bool first = leftmost() == original, last = rightmost() == original;
		original->get_parent()->set_child(original->position(), replacement);
		if (first) root()->fields_.parent = replacement;
		if (last) __rightmost() = replacement;
		++__size();
	}
	delete_leaf_node(original);
	return iterator(replacement, position);
}

template <typename P> template <typename... Args>
inline typename btree<P>::iterator
btree<P>::emplace_nonfull(iterator iter, Args&&... args) {
	auto* node = iter.node;
	const int count = node->count();
	if (iter.position == count) {
		node->insert_value(count, std::forward<Args>(args)...);
	} else if constexpr (std::is_nothrow_move_constructible<mutable_value_type>::value &&
			sizeof(mutable_value_type) <= P::kTargetNodeSize) {
		// Relocation cannot throw: build the incoming value first, then shift.
		mutable_value_type incoming(std::forward<Args>(args)...);
		for (int j = count - 1; j >= iter.position; --j) {
			node->construct_value(j + 1, std::move(node->mutable_value(j)));
			node->destroy_value(j);
		}
		node->construct_value(iter.position, std::move(incoming));
		node->set_count(count + 1);
	} else {
		return replace_leaf(node, iter.position, node->max_count(), std::forward<Args>(args)...);
	}
	if (!root()->is_leaf()) ++__size();
	return iter;
}

template <typename P> template <typename... Args>
inline typename btree<P>::iterator
btree<P>::internal_emplace(const_iterator hint, Args&&... args) {
	iterator iter(hint.node, hint.position);
	if (!iter.node->is_leaf()) {
		// We can't insert on an internal node. Instead, we'll insert after the
		// previous value which is guaranteed to be on a leaf node.
		--iter;
		++iter.position;
	}
	if (iter.node->count() < iter.node->max_count()) {
		return emplace_nonfull(iter, std::forward<Args>(args)...);
	}
	if (iter.node->count() == iter.node->max_count()) {
		// Make room in the leaf for the new item.
		if (iter.node->max_count() < kNodeValues) {
			// Insertion into the root where the root is smaller that the full node
			// size. Simply grow the size of the root node.
			assert(iter.node == root());
			return replace_leaf(iter.node, iter.position, std::min<int>(kNodeValues, 2 * iter.node->max_count()), std::forward<Args>(args)...);
		} else if constexpr (std::is_nothrow_move_constructible<mutable_value_type>::value &&
				sizeof(mutable_value_type) <= P::kTargetNodeSize) {
			// Construct the incoming value before reshaping the tree: a refusal
			// leaves the tree untouched, and arguments that alias contained
			// values are read before any relocation.
			mutable_value_type incoming(std::forward<Args>(args)...);
			rebalance_or_split(iter);
			++__size();
			iter.node->insert_value(iter.position, std::move(incoming));
			return iter;
		} else {
			// The value is built in its slot after the split. The split biases
			// toward the insertion side and can leave that leaf empty, so a
			// failed construction repairs it, and the size counts the value
			// only once it exists.
			rebalance_or_split(iter);
			try {
				iter.node->insert_value(iter.position, std::forward<Args>(args)...);
			} catch (...) {
				repair_failed_insertion(const_iterator(iter.node, iter.position));
				throw;
			}
			++__size();
			return iter;
		}
	}
	iter.node->insert_value(iter.position, std::forward<Args>(args)...);
	return iter;
}

template <typename P> template <typename IterType>
inline std::pair<IterType, int> btree<P>::internal_locate(const key_type& key, IterType iter) const {
	return internal_locate_type::dispatch(key, *this, iter);
}

template <typename P> template <typename IterType>
inline std::pair<IterType, int> btree<P>::internal_locate_plain_compare(const key_type& key, IterType iter) const {
	for (;;) {
		iter.position = iter.node->lower_bound(key, key_comp());
		if (iter.node->is_leaf()) {
			break;
		}
		iter.node = iter.node->child(iter.position);
	}
	return std::make_pair(iter, 0);
}

template <typename P> template <typename IterType>
inline std::pair<IterType, int> btree<P>::internal_locate_compare_to(const key_type& key, IterType iter) const {
	for (;;) {
		int res = iter.node->lower_bound(key, key_comp());
		iter.position = res&  kMatchMask;
		if (res&  kExactMatch) {
			return std::make_pair(iter, static_cast<int>(kExactMatch));
		}
		if (iter.node->is_leaf()) {
			break;
		}
		iter.node = iter.node->child(iter.position);
	}
	return std::make_pair(iter, -kExactMatch);
}

template <typename P> template <typename IterType>
IterType btree<P>::internal_lower_bound(const key_type& key, IterType iter) const {
	if (iter.node) {
		for (;;) {
			iter.position =
					iter.node->lower_bound(key, key_comp())&  kMatchMask;
			if (iter.node->is_leaf()) {
				break;
			}
			iter.node = iter.node->child(iter.position);
		}
		iter = internal_last(iter);
	}
	return iter;
}

template <typename P> template <typename IterType>
IterType btree<P>::internal_upper_bound(const key_type& key, IterType iter) const {
	if (iter.node) {
		for (;;) {
			iter.position = iter.node->upper_bound(key, key_comp());
			if (iter.node->is_leaf()) {
				break;
			}
			iter.node = iter.node->child(iter.position);
		}
		iter = internal_last(iter);
	}
	return iter;
}

template <typename P> template <typename IterType>
IterType btree<P>::internal_find_unique(const key_type& key, IterType iter) const {
	if (iter.node) {
		std::pair<IterType, int> res = internal_locate(key, iter);
		if (res.second == kExactMatch) {
			return res.first;
		}
		if (!res.second) {
			iter = internal_last(res.first);
			if (iter.node && !compare_keys(key, iter.key())) {
				return iter;
			}
		}
	}
	return IterType(nullptr, 0);
}

template <typename P> template <typename IterType>
IterType btree<P>::internal_find_multi(const key_type& key, IterType iter) const {
	if (iter.node) {
		iter = internal_lower_bound(key, iter);
		if (iter.node) {
			iter = internal_last(iter);
			if (iter.node && !compare_keys(key, iter.key())) {
				return iter;
			}
		}
	}
	return IterType(nullptr, 0);
}

template <typename P>
void btree<P>::internal_clear(node_type* node) {
	if (!node->is_leaf()) {
		for (int i = 0; i <= node->count(); ++i) {
			internal_clear(node->child(i));
		}
		if (node == root()) {
			delete_internal_root_node();
		} else {
			delete_internal_node(node);
		}
	} else {
		delete_leaf_node(node);
	}
}

template <typename P>
void btree<P>::internal_dump(std::ostream& os, const node_type* node, int level) const {
	for (int i = 0; i < node->count(); ++i) {
		if (!node->is_leaf()) {
			internal_dump(os, node->child(i), level + 1);
		}
		for (int j = 0; j < level; ++j) {
			os << "  ";
		}
		os << node->key(i) << " [" << level << "]\n";
	}
	if (!node->is_leaf()) {
		internal_dump(os, node->child(node->count()), level + 1);
	}
}

template <typename P>
int btree<P>::internal_verify(const node_type* node, const key_type* lo, const key_type* hi) const {
	assert(node->count() > 0);
	assert(node->count() <= node->max_count());
	if (lo) {
		assert(!compare_keys(node->key(0), *lo));
	}
	if (hi) {
		assert(!compare_keys(*hi, node->key(node->count() - 1)));
	}
	for (int i = 1; i < node->count(); ++i) {
		assert(!compare_keys(node->key(i), node->key(i - 1)));
	}
	int count = node->count();
	if (!node->is_leaf()) {
		for (int i = 0; i <= node->count(); ++i) {
			assert(node->child(i) != nullptr);
			assert(node->child(i)->get_parent() == node);
			assert(node->child(i)->position() == i);
			count += internal_verify(node->child(i),
				(i == 0) ? lo : &node->key(i - 1),
				(i == node->count()) ? hi : &node->key(i));
		}
	}
	return count;
}

} // namespace btree

#endif  // BTREE_BTREE_TREE_H__
