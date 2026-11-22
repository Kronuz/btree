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
 * btree_node: the fixed-header node with trailing slots, with its member definitions.
 * A real header, but the pieces are meant to be used through btree/btree.h.
 */

// btree_node: the node layout, its fixed header and the trailing key/value/child arrays.

#ifndef BTREE_BTREE_NODE_H__
#define BTREE_BTREE_NODE_H__

#include <assert.h>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "btree_params.h"

namespace btree {

// Nodes contain a complete fixed header and separately constructed trailing
// slots. Short roots allocate only their current value capacity.
template <typename Params>
class btree_node {
public:
	typedef Params params_type;
	typedef btree_node<Params> self_type;
	typedef typename Params::key_type key_type;
	typedef typename Params::data_type data_type;
	typedef typename Params::value_type value_type;
	typedef typename Params::mutable_value_type mutable_value_type;
	typedef typename Params::slot_type slot_type;
	typedef typename Params::pointer pointer;
	typedef typename Params::const_pointer const_pointer;
	typedef typename Params::reference reference;
	typedef typename Params::const_reference const_reference;
	typedef typename Params::key_compare key_compare;
	typedef typename Params::size_type size_type;
	typedef typename Params::difference_type difference_type;

	typedef typename Params::allocator_type allocator_type;
	typedef typename Params::allocator_traits allocator_traits;

	// Typedefs for the various types of node searches.
	typedef btree_linear_search_plain_compare<key_type, self_type, key_compare> linear_search_plain_compare_type;
	typedef btree_linear_search_compare_to<key_type, self_type, key_compare> linear_search_compare_to_type;
	typedef btree_binary_search_plain_compare<key_type, self_type, key_compare> binary_search_plain_compare_type;
	typedef btree_binary_search_compare_to<key_type, self_type, key_compare> binary_search_compare_to_type;
	// If we have a valid key-compare-to type, use linear_search_compare_to,
	// otherwise use linear_search_plain_compare.
	typedef std::conditional_t<Params::is_key_compare_to::value,
		linear_search_compare_to_type,
		linear_search_plain_compare_type> linear_search_type;
	// If we have a valid key-compare-to type, use binary_search_compare_to,
	// otherwise use binary_search_plain_compare.
	typedef std::conditional_t<Params::is_key_compare_to::value,
		binary_search_compare_to_type,
		binary_search_plain_compare_type> binary_search_type;
	// If the key is an integral or floating point type, use linear search which
	// is faster than binary search for such types. Might be wise to also
	// configure linear search based on node-size.
	typedef std::conditional_t<std::is_integral<key_type>::value || std::is_floating_point<key_type>::value,
		linear_search_type, binary_search_type> search_type;

	struct base_fields {
		typedef typename Params::node_count_type field_type;

		// A boolean indicating whether the node is a leaf or not.
		bool leaf;
		// The position of the node in the node's parent.
		field_type position;
		// The maximum number of values the node can hold.
		field_type max_count;
		// The count of the number of values in the node.
		field_type count;
		// A pointer to the node's parent.
		btree_node* parent;
	};

	enum {
		kValueSize = params_type::kValueSize,
		kTargetNodeSize = params_type::kTargetNodeSize,

		// Compute how many values we can fit onto a leaf node.
		kNodeTargetValues = (kTargetNodeSize - sizeof(base_fields)) / kValueSize,
		// We need a minimum of 3 values per internal node in order to perform
		// splitting (1 value for the two nodes involved in the split and 1 value
		// propagated to the parent as the delimiter for the split).
		kNodeValues = kNodeTargetValues >= 3 ? kNodeTargetValues : 3,

		kExactMatch = 1 << 30,
		kMatchMask = kExactMatch - 1,
	};

	// Size descriptors only; compact allocations never contain these objects.
	struct leaf_fields : public base_fields {
		// The array of values. Only the first count of these values have been
		// constructed and are valid.
		value_type values[kNodeValues];
	};

	struct internal_fields : public leaf_fields {
		// The array of child pointers. The keys in children_[i] are all less than
		// key(i). The keys in children_[i + 1] are all greater than key(i). There
		// are always count + 1 children.
		btree_node* children[kNodeValues + 1];
	};

	struct root_fields : public internal_fields {
		btree_node* rightmost;
		size_type size;
	};
	struct root_metadata {
		btree_node* rightmost;
		size_type size;
	};
	struct alignas(alignof(value_type)) aligned_allocation_unit { unsigned char byte; };
	using allocation_unit = std::conditional_t<(alignof(value_type) <= alignof(std::max_align_t)),
		char, aligned_allocation_unit>;

	static constexpr std::size_t add_size(std::size_t left, std::size_t right) {
		if (right > std::numeric_limits<std::size_t>::max() - left)
			throw std::length_error("btree node size overflow");
		return left + right;
	}
	static constexpr std::size_t align_size(std::size_t bytes, std::size_t alignment) {
		return add_size(bytes, alignment - 1) & ~(alignment - 1);
	}
	static constexpr std::size_t values_offset() {
		return align_size(sizeof(base_fields), alignof(value_type));
	}
	static constexpr std::size_t leaf_size(std::size_t capacity) {
		if (capacity > (std::numeric_limits<std::size_t>::max() - values_offset()) / sizeof(value_type))
			throw std::length_error("btree node size overflow");
		return values_offset() + capacity * sizeof(value_type);
	}
	static constexpr std::size_t children_offset() {
		return align_size(leaf_size(kNodeValues), alignof(btree_node*));
	}
	static constexpr std::size_t internal_size() {
		return add_size(children_offset(), (std::size_t(kNodeValues) + 1) * sizeof(btree_node*));
	}
	static constexpr std::size_t root_offset() {
		return align_size(internal_size(), alignof(root_metadata));
	}
	static constexpr std::size_t root_size() { return add_size(root_offset(), sizeof(root_metadata)); }
	static constexpr std::size_t allocation_count(std::size_t bytes) {
		return align_size(bytes, sizeof(allocation_unit)) / sizeof(allocation_unit);
	}
	static constexpr std::size_t allocation_size(std::size_t bytes) {
		return allocation_count(bytes) * sizeof(allocation_unit);
	}

public:
	// Getter/setter for whether this is a leaf node or not. This value doesn't
	// change after the node is created.
	bool is_leaf() const {
		return fields_.leaf;
	}

	// Getter for the position of this node in its parent.
	int position() const {
		return fields_.position;
	}

	// Getter/setter for the number of values stored in this node.
	int count() const {
		return fields_.count;
	}
	void set_count(int v) {
		assert(v >= 0);
		assert(v <= fields_.max_count);
		fields_.count = v;
	}
	int max_count() const {
		return fields_.max_count;
	}

	// Getter for the parent of this node.
	btree_node* get_parent() const {
		return fields_.parent;
	}
	// Getter for whether the node is the root of the tree. The parent of the
	// root of the tree is the leftmost node in the tree which is guaranteed to
	// be a leaf.
	bool is_root() const {
		return fields_.parent->is_leaf();
	}
	void make_root() {
		assert(fields_.parent->is_root());
		fields_.parent = fields_.parent->get_parent();
	}

	// Getter for the rightmost root node field. Only valid on the root node.
	btree_node* rightmost() const {
		return root_data().rightmost;
	}
	btree_node*& __rightmost() {
		return root_data().rightmost;
	}

	// Getter for the size root node field. Only valid on the root node.
	size_type size() const {
		return root_data().size;
	}
	size_type& __size() {
		return root_data().size;
	}

	// Getters for the key/value at position i in the node.
	const key_type& key(int i) const {
		return params_type::get_key(slot_value(i));
	}
	reference value(int i) {
		return params_type::element(*std::launder(value_slot(i)));
	}
	const_reference value(int i) const {
		return params_type::element(*std::launder(value_slot(i)));
	}
	// The live object in slot i; relocation moves from it, so keys move.
	mutable_value_type& mutable_value(int i) {
		return slot_value(i);
	}

	// Swap value i in this node with value j in node x.
	void swap_value(int i, btree_node* x, int j) {
		assert(x != this || i != j);
		params_type::swap(slot_value(i), x->slot_value(j));
	}

	// Swap value i in this node with value i in node x.
	void swap_value(int i, btree_node* x) {
		assert(x != this);
		params_type::swap(slot_value(i), x->slot_value(i));
	}

	// Swap value i with value j.
	void swap_value(int i, int j) {
		assert(i != j);
		params_type::swap(slot_value(i), slot_value(j));
	}

	// Move value i in this node to value j in node x.
	void move_value(int i, btree_node* x, int j) {
		assert(x != this || i != j);
		x->construct_value(j, std::move(slot_value(i)));
		destroy_value(i);
	}

	// Move value i in this node to value i in node x.
	void move_value(int i, btree_node* x) {
		assert(x != this);
		x->construct_value(i, std::move(slot_value(i)));
		destroy_value(i);
	}

	// Move value i to value j.
	void move_value(int i, int j) {
		assert(i != j);
		construct_value(j, std::move(slot_value(i)));
		destroy_value(i);
	}

	// Getters/setter for the child at position i in the node.
	btree_node* child(int i) const {
		assert(!is_leaf());
		return slot_child(i);
	}

	// Inactive child slots have no invariant. Keeping their state independent
	// of NDEBUG avoids coupling inline mutations to a caller's build mode.
	void reset_child(int) {
	}

	void set_child(int i, btree_node* c) {
		assert(!is_leaf());
		assert(c != nullptr);
		slot_child(i) = c;
		c->fields_.parent = this;
		c->fields_.position = i;
	}

	// Swap child i in this node with child j in node x.
	void swap_child(int i, btree_node* x, int j) {
		assert(x != this || i != j);
		assert(!is_leaf());
		assert(!x->is_leaf());
		assert(slot_child(i) != nullptr);
		assert(x->slot_child(j) != nullptr);
		auto& a = slot_child(i);
		auto& b = x->slot_child(j);
		btree_swap_helper(a, b);
		a->fields_.parent = this;
		a->fields_.position = i;
		b->fields_.parent = x;
		b->fields_.position = j;
	}

	// Swap child i in this node with child i in node x.
	void swap_child(int i, btree_node* x) {
		assert(x != this);
		assert(!is_leaf());
		assert(!x->is_leaf());
		assert(slot_child(i) != nullptr);
		assert(x->slot_child(i) != nullptr);
		auto& a = slot_child(i);
		auto& b = x->slot_child(i);
		btree_swap_helper(a, b);
		a->fields_.parent = this;
		b->fields_.parent = x;
	}

	// Swap child i with child j.
	void swap_child(int i, int j) {
		assert(i != j);
		assert(!is_leaf());
		assert(slot_child(i) != nullptr);
		assert(slot_child(j) != nullptr);
		auto& a = slot_child(i);
		auto& b = slot_child(j);
		btree_swap_helper(a, b);
		a->fields_.position = i;
		b->fields_.position = j;
	}

	// Move child i in this node to child j in node x.
	void move_child(int i, btree_node* x, int j) {
		assert(x != this || i != j);
		assert(!is_leaf());
		assert(!x->is_leaf());
		assert(slot_child(i) != nullptr);
		auto c = slot_child(i);
		x->slot_child(j) = c;
		c->fields_.position = j;
		c->fields_.parent = x;
	}

	// Move child i in this node to child i in node x.
	void move_child(int i, btree_node* x) {
		assert(x != this);
		assert(!is_leaf());
		assert(!x->is_leaf());
		assert(slot_child(i) != nullptr);
		auto c = slot_child(i);
		x->slot_child(i) = c;
		c->fields_.parent = x;
	}

	// Move child i to child j.
	void move_child(int i, int j) {
		assert(i != j);
		assert(!is_leaf());
		assert(slot_child(i) != nullptr);
		auto c = slot_child(i);
		slot_child(j) = c;
		c->fields_.position = j;
	}

	// Returns the position of the first value whose key is not less than k.
	template <typename Compare>
	int lower_bound(const key_type& k, const Compare& comp) const {
		return search_type::lower_bound(k, *this, comp);
	}
	// Returns the position of the first value whose key is greater than k.
	template <typename Compare>
	int upper_bound(const key_type& k, const Compare& comp) const {
		return search_type::upper_bound(k, *this, comp);
	}

	// Heterogeneous lower_bound (see btree_is_transparent), used only by
	// find/count/contains -- not upper_bound/equal_range.
	template <typename K, typename Compare, typename = std::enable_if_t<btree_is_transparent<Compare>::value>>
	int lower_bound(const K& k, const Compare& comp) const {
		return std::is_integral<key_type>::value || std::is_floating_point<key_type>::value
			? linear_search_plain_compare(k, 0, count(), comp)
			: binary_search_plain_compare(k, 0, count(), comp);
	}

	// Returns the position of the first value whose key is not less than k
	// using linear search performed using plain compare.
	template <typename Compare>
	int linear_search_plain_compare(const key_type& k, int s, int e, const Compare& comp) const {
		while (s < e) {
			if (!btree_compare_keys(comp, key(s), k)) {
				break;
			}
			++s;
		}
		return s;
	}

	// Heterogeneous sibling: K need not be key_type; calls comp(...)
	// directly (a transparent Compare is always a plain boolean functor).
	template <typename K, typename Compare>
	int linear_search_plain_compare(const K& k, int s, int e, const Compare& comp) const {
		while (s < e) {
			if (!comp(key(s), k)) {
				break;
			}
			++s;
		}
		return s;
	}

	// Returns the position of the first value whose key is not less than k
	// using linear search performed using compare-to.
	template <typename Compare>
	int linear_search_compare_to(const key_type& k, int s, int e, const Compare& comp) const {
		while (s < e) {
			int c = comp(key(s), k);
			if (c == 0) {
				return s | kExactMatch;
			} else if (c > 0) {
				break;
			}
			++s;
		}
		return s;
	}

	// Returns the position of the first value whose key is not less than k
	// using binary search performed using plain compare.
	template <typename Compare>
	int binary_search_plain_compare(const key_type& k, int s, int e, const Compare& comp) const {
		while (s != e) {
			int mid = (s + e) / 2;
			if (btree_compare_keys(comp, key(mid), k)) {
				s = mid + 1;
			} else {
				e = mid;
			}
		}
		return s;
	}

	// Heterogeneous sibling of linear_search_plain_compare above.
	template <typename K, typename Compare>
	int binary_search_plain_compare(const K& k, int s, int e, const Compare& comp) const {
		while (s != e) {
			int mid = (s + e) / 2;
			if (comp(key(mid), k)) {
				s = mid + 1;
			} else {
				e = mid;
			}
		}
		return s;
	}

	// Returns the position of the first value whose key is not less than k
	// using binary search performed using compare-to.
	template <typename CompareTo>
	int binary_search_compare_to(const key_type& k, int s, int e, const CompareTo& comp) const {
		while (s != e) {
			int mid = (s + e) / 2;
			int c = comp(key(mid), k);
			if (c < 0) {
				s = mid + 1;
			} else if (c > 0) {
				e = mid;
			} else {
				// Need to return the first value whose key is not less than k,
				// which requires continuing the binary search. Note that we are
				// guaranteed that the result is an exact match because
				// if "key(mid-1) < k" the call to binary_search_compare_to()
				// will return "mid".
				s = binary_search_compare_to(k, s, mid, comp);
				return s | kExactMatch;
			}
		}
		return s;
	}

	// Inserts the value x at position i, shifting all existing values
	// and children at positions >= i to the right by 1.
	template <typename... Args>
	void insert_value(int i, Args&&... args);

	// Removes the value at position i, shifting all existing values
	// and children at positions > i to the left by 1.
	void remove_value(int i);

	// Rebalances a node with its right sibling.
	void rebalance_right_to_left(btree_node* sibling, int to_move);
	void rebalance_left_to_right(btree_node* sibling, int to_move);

	// Splits a node, moving a portion of the node's values to its right sibling.
	void split(btree_node* sibling, int insert_position);

	// Merges a node with its right sibling, moving all of the values
	// and the delimiting key in the parent node onto itself.
	void merge(btree_node* sibling);

	// Swap the contents of "this" and "src".
	void swap(btree_node* src);

	// Node allocation/deletion routines.
	static btree_node* init_leaf(void* storage, btree_node* parent, int max_count) {
		return ::new (storage) btree_node(parent, max_count);
	}

	static btree_node* init_internal(void* storage, btree_node* parent) {
		btree_node* n = init_leaf(storage, parent, kNodeValues);
		n->fields_.leaf = false;
		for (int i = 0; i <= kNodeValues; ++i)
			::new (static_cast<void*>(n->child_slot(i))) btree_node*;
		return n;
	}

	static btree_node* init_root(void* storage, btree_node* parent) {
		btree_node* n = init_internal(storage, parent);
		::new (static_cast<void*>(n->storage() + root_offset())) root_metadata{parent, parent ? parent->count() : 0};
		return n;
	}

	void destroy() {
		for (int i = 0; i < count(); ++i) {
			destroy_value(i);
		}
	}

private:
	template <typename> friend class btree;
	btree_node(btree_node* parent, int capacity)
		: fields_{true, 0, static_cast<typename base_fields::field_type>(capacity), 0, parent} {}
	char* storage() { return reinterpret_cast<char*>(this); }
	const char* storage() const { return reinterpret_cast<const char*>(this); }
	slot_type* value_slot(int i) {
		return reinterpret_cast<slot_type*>(storage() + values_offset() + std::size_t(i) * sizeof(value_type));
	}
	const slot_type* value_slot(int i) const {
		return reinterpret_cast<const slot_type*>(storage() + values_offset() + std::size_t(i) * sizeof(value_type));
	}
	mutable_value_type& slot_value(int i) { return params_type::mutable_element(*std::launder(value_slot(i))); }
	const mutable_value_type& slot_value(int i) const { return params_type::mutable_element(*std::launder(value_slot(i))); }
	btree_node** child_slot(int i) {
		return reinterpret_cast<btree_node**>(storage() + children_offset() + std::size_t(i) * sizeof(btree_node*));
	}
	btree_node* const* child_slot(int i) const {
		return reinterpret_cast<btree_node* const*>(storage() + children_offset() + std::size_t(i) * sizeof(btree_node*));
	}
	btree_node*& slot_child(int i) { return *std::launder(child_slot(i)); }
	btree_node* const& slot_child(int i) const { return *std::launder(child_slot(i)); }
	root_metadata& root_data() {
		return *std::launder(reinterpret_cast<root_metadata*>(storage() + root_offset()));
	}
	const root_metadata& root_data() const {
		return *std::launder(reinterpret_cast<const root_metadata*>(storage() + root_offset()));
	}

	template <typename... Args>
	void construct_value(slot_type* v, Args&&... args) {
		params_type::construct(v, std::forward<Args>(args)...);
		// FIXME: The above should use allocator_traits, but allocator
		// object is not available here at the nodes, is in the tree:
		// allocator_type& alloc = allocator();
		// allocator_traits::construct(alloc, v, std::forward<Args>(args)...);
	}

	template <typename... Args>
	void construct_value(int i, Args&&... args) {
		assert(i >= 0);
		assert(i < fields_.max_count);
		construct_value(value_slot(i), std::forward<Args>(args)...);
	}

	void destroy_value(slot_type* v) {
		params_type::destroy(v);
		// FIXME: The above should use allocator_traits, but allocator
		// object is not available here at the nodes, is in the tree:
		// allocator_type& alloc = allocator();
		// allocator_traits::destroy(alloc, v);
	}

	void destroy_value(int i) {
		assert(i >= 0);
		assert(i < fields_.max_count);
		destroy_value(std::launder(value_slot(i)));
	}

private:
	base_fields fields_;

private:
	btree_node(const btree_node&);
	void operator=(const btree_node&);
};

////
// btree_node methods
template <typename P> template <typename... Args>
inline void btree_node<P>::insert_value(int i, Args&&... args) {
	auto cnt = count();

	assert(i <= cnt);

	// Initialize new value at the end.
	construct_value(cnt, std::forward<Args>(args)...);

	// Move initialized value to the correct position.
	if (cnt > i) {
		if (is_leaf()) {
			for (int j = cnt; j > i; --j) {
				swap_value(j - 1, j);
			}
		} else {
			for (int j = cnt; j > i; --j) {
				swap_value(j - 1, j);
				move_child(j, j + 1);
			}
		}
	}

	// Increase number of items
	set_count(cnt + 1);
}

template <typename P>
inline void btree_node<P>::remove_value(int i) {
	auto cnt = count();

	// Move the value to the end.
	if (is_leaf()) {
		for (int j = i + 1; j < cnt; ++j) {
			swap_value(j, j - 1);
		}
	} else {
		assert(child(i + 1)->count() == 0);
		for (int j = i + 1; j < cnt; ++j) {
			swap_value(j, j - 1);
			move_child(j + 1, j);
		}
		reset_child(cnt);
	}

	// Decrease number of items.
	set_count(--cnt);

	// Finally, destroy value.
	destroy_value(cnt);
}

template <typename P>
void btree_node<P>::rebalance_right_to_left(btree_node* src, int to_move) {
	auto cnt = count();
	auto src_cnt = src->count();

	assert(get_parent() == src->get_parent());
	assert(position() + 1 == src->position());
	assert(src_cnt >= cnt);
	assert(to_move >= 1);
	assert(to_move <= src_cnt);

	// Move the delimiting value to the left node.
	get_parent()->move_value(position(), this, cnt);

	// Move the new delimiting value from the right node.
	src->move_value(to_move - 1, get_parent(), position());

	if (is_leaf()) {
		// Move the values from the right to the left node.
		for (int i = 1; i < to_move; ++i) {
			src->move_value(i - 1, this, cnt + i);
		}
		// Shift the values in the right node to their correct position.
		for (int i = to_move; i < src_cnt; ++i) {
			src->move_value(i, i - to_move);
		}
	} else {
		// Move the values and child pointsts from the right to the left node.
		src->move_child(0, this, 1 + cnt);
		for (int i = 1; i < to_move; ++i) {
			src->move_value(i - 1, this, cnt + i);
			src->move_child(i, this, 1 + cnt + i);
		}
		// Shift the values and child pointsts in the right node to their correct position.
		for (int i = to_move; i < src_cnt; ++i) {
			src->move_value(i, i - to_move);
			src->move_child(i, i - to_move);
		}
		src->move_child(src_cnt, src_cnt - to_move);
	}

	// Fixup the counts on the src and dst nodes.
	set_count(cnt + to_move);
	src->set_count(src_cnt - to_move);
}

template <typename P>
void btree_node<P>::rebalance_left_to_right(btree_node* dst, int to_move) {
	auto cnt = count();
	auto dst_cnt = dst->count();

	assert(get_parent() == dst->get_parent());
	assert(position() + 1 == dst->position());
	assert(cnt >= dst_cnt);
	assert(to_move >= 1);
	assert(to_move <= cnt);

	// Make room in the right node for the new values.
	for (int i = dst_cnt - 1; i >= 0; --i) {
		dst->move_value(i, i + to_move);
	}

	// Move the delimiting value to the right node.
	get_parent()->move_value(position(), dst, to_move - 1);

	// Move the new delimiting value from the left node.
	this->move_value(cnt - to_move, get_parent(), position());

	if (is_leaf()) {
		// Move the values from the left to the right node.
		for (int i = 1; i < to_move; ++i) {
			move_value(cnt - to_move + i, dst, i - 1);
		}
	} else {
		// Move the values and child pointers from the left to the right node.
		for (int i = dst_cnt; i >= 0; --i) {
			dst->move_child(i, i + to_move);
		}
		// Move the values and child pointers from the left to the right node.
		for (int i = 1; i < to_move; ++i) {
			move_value(cnt - to_move + i, dst, i - 1);
			move_child(cnt - to_move + i, dst, i - 1);
		}
		move_child(cnt, dst, to_move - 1);
	}

	// Fixup the counts on the src and dst nodes.
	set_count(cnt - to_move);
	dst->set_count(dst_cnt + to_move);
}

template <typename P>
void btree_node<P>::split(btree_node* dst, int insert_position) {
	auto cnt = count();
	auto dst_cnt = dst->count();

	assert(dst_cnt == 0);

	// We bias the split based on the position being inserted. If we're
	// inserting at the beginning of the left node then bias the split to put
	// more values on the right node. If we're inserting at the end of the
	// right node then bias the split to put more values on the left node.
	if (insert_position == 0) {
		dst_cnt = cnt - 1;
		dst->set_count(dst_cnt);
	} else if (insert_position != max_count()) {
		dst_cnt = cnt / 2;
		dst->set_count(dst_cnt);
	}
	assert(cnt > dst_cnt);
	cnt -= dst_cnt;
	set_count(cnt);

	if (is_leaf()) {
		// Move values from the left sibling to the right sibling.
		for (int i = 0; i < dst_cnt; ++i) {
			move_value(cnt + i, dst, i);
		}
	} else {
		// Move values and child pointers from the left sibling to the right sibling.
		for (int i = 0; i < dst_cnt; ++i) {
			move_value(cnt + i, dst, i);
			move_child(cnt + i, dst, i);
		}
		move_child(cnt + dst_cnt, dst, dst_cnt);
	}

	// The split key is the largest value in the left sibling.
	set_count(--cnt);
	get_parent()->insert_value(position(), std::move(slot_value(cnt)));
	destroy_value(cnt);
	get_parent()->set_child(position() + 1, dst);
}

template <typename P>
void btree_node<P>::merge(btree_node* src) {
	auto cnt = count();
	auto src_cnt = src->count();
	auto parent = get_parent();
	auto parent_cnt = parent->count();

	assert(parent == src->get_parent());
	assert(position() + 1 == src->position());

	// Move the delimiting value to the left node.
	parent->move_value(position(), this, cnt);

	// Shift the values in the parent node to their correct position.
	for (int i = position() + 1; i < parent_cnt; ++i) {
		parent->move_value(i, i - 1);
		parent->move_child(i + 1, i);
	}
	parent->reset_child(parent_cnt);

	if (is_leaf()) {
		// Move the values from the right to the left node.
		for (int i = 0; i < src_cnt; ++i) {
			src->move_value(i, this, 1 + cnt + i);
		}
	} else {
		// Move the values and child pointers from the right to the left node.
		for (int i = 0; i < src_cnt; ++i) {
			src->move_value(i, this, 1 + cnt + i);
			src->move_child(i, this, 1 + cnt + i);
		}
		src->move_child(src_cnt, this, 1 + cnt + src_cnt);
	}

	// Fixup the counts on the parent, src and dst nodes.
	parent->set_count(parent_cnt - 1);
	set_count(1 + cnt + src_cnt);
	src->set_count(0);
}

template <typename P>
void btree_node<P>::swap(btree_node* x) {
	auto cnt = count();
	auto x_cnt = x->count();

	int min = std::min(cnt, x_cnt);

	assert(is_leaf() == x->is_leaf());

	if (is_leaf()) {
		// Swap the values.
		for (int i = 0; i < min; ++i) {
			swap_value(i, x);
		}
		for (int i = min; i < x_cnt; ++i) {
			x->move_value(i, this);
		}
		for (int i = min; i < cnt; ++i) {
			move_value(i, x);
		}
	} else {
		// Swap the values and child pointers.
		for (int i = 0; i < min; ++i) {
			swap_value(i, x);
			swap_child(i, x);
		}
		swap_child(min, x);
		for (int i = min; i < x_cnt; ++i) {
			x->move_value(i, this);
			x->move_child(i + 1, this);
		}
		for (int i = min; i < cnt; ++i) {
			move_value(i, x);
			move_child(i + 1, x);
		}
	}

	// Swap the counts.
	btree_swap_helper(fields_.count, x->fields_.count);
}

} // namespace btree

#endif  // BTREE_BTREE_NODE_H__
