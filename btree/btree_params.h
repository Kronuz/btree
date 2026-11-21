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
 * Type parameter structs for sets and maps, and the node search dispatchers.
 * Internal: included only by btree/btree.h.
 */

#ifndef BTREE_BTREE_PARAMS_H__
#define BTREE_BTREE_PARAMS_H__

namespace btree {

template <typename Key, typename Compare,
		  typename Alloc, int TargetNodeSize, int ValueSize>
struct btree_common_params {
	// If Compare is derived from btree_key_compare_to_tag then use it as the
	// key_compare type. Otherwise, use btree_key_compare_to_adapter<> which will
	// fall-back to Compare if we don't have an appropriate specialization.
	typedef std::conditional_t<btree_is_key_compare_to<Compare>::value,
		Compare,
		btree_key_compare_to_adapter<Compare>> key_compare;
	// A type which indicates if we have a key-compare-to functor or a plain old
	// key-compare functor.
	typedef btree_is_key_compare_to<key_compare> is_key_compare_to;

	typedef Key key_type;
	typedef ssize_t size_type;
	typedef ptrdiff_t difference_type;

	typedef Alloc allocator_type;
	typedef std::allocator_traits<allocator_type> allocator_traits;
	typedef typename allocator_traits::template rebind_alloc<char> internal_allocator_type;
	typedef std::allocator_traits<internal_allocator_type> internal_allocator_traits;

	enum {
		kTargetNodeSize = TargetNodeSize,

		// Available space for values.  This is largest for leaf nodes,
		// which has overhead no fewer than two pointers.
		kNodeValueSpace = TargetNodeSize - 2 * sizeof(void*),
	};

	// This is an integral type large enough to hold as many
	// ValueSize-values as will fit a node of TargetNodeSize bytes.
	typedef std::conditional_t<(kNodeValueSpace / ValueSize) >= 256,
		uint16_t,
		uint8_t> node_count_type;
};

// Layout probe for map slots. A map stores std::pair<Key, T> (the "mutable"
// pair) and exposes it as std::pair<const Key, T>, so relocation can move keys
// instead of copying them. The two pair types must have identical size,
// alignment and member offsets; the mirror structs below measure the offsets
// without requiring pair itself to be standard layout.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
template <typename Key, typename Data>
struct btree_map_slot_layout {
	struct mutable_mirror { Key first; Data second; };
	struct const_mirror { const Key first; Data second; };
	typedef std::pair<Key, Data> mutable_pair;
	typedef std::pair<const Key, Data> const_pair;
	static constexpr bool compatible =
		sizeof(mutable_pair) == sizeof(const_pair) &&
		alignof(mutable_pair) == alignof(const_pair) &&
		sizeof(mutable_mirror) == sizeof(const_mirror) &&
		alignof(mutable_mirror) == alignof(const_mirror) &&
		offsetof(mutable_mirror, first) == offsetof(const_mirror, first) &&
		offsetof(mutable_mirror, second) == offsetof(const_mirror, second) &&
		sizeof(mutable_pair) == sizeof(mutable_mirror) &&
		std::is_standard_layout<mutable_pair>::value == std::is_standard_layout<const_pair>::value;
};
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// A parameters structure for holding the type parameters for a map.
template <typename Key, typename Data, typename Compare, typename Alloc, int TargetNodeSize>
struct btree_map_params : public btree_common_params<Key, Compare, Alloc, TargetNodeSize, sizeof(Key) + sizeof(Data)> {
	typedef Data data_type;
	typedef Data mapped_type;
	typedef std::pair<const Key, data_type> value_type;
	typedef std::pair<Key, data_type> mutable_value_type;
	typedef value_type* pointer;
	typedef const value_type* const_pointer;
	typedef value_type& reference;
	typedef const value_type& const_reference;

	// Each slot is a live union whose active member is the mutable pair. The
	// tree relocates through mutable_value and hands out value, as Abseil's
	// map_slot_type does, so no const object is ever modified.
	union slot_type {
		value_type value;
		mutable_value_type mutable_value;
		slot_type() {}
		~slot_type() {}
	};
	static_assert(btree_map_slot_layout<Key, Data>::compatible,
		"pair<const Key, T> and pair<Key, T> must have the same size, alignment and member offsets");
	static_assert(sizeof(slot_type) == sizeof(value_type) && alignof(slot_type) == alignof(value_type),
		"map slot must have the size and alignment of its value");

	enum {
		kValueSize = sizeof(Key) + sizeof(data_type),
	};

	static reference element(slot_type& slot) { return slot.value; }
	static const_reference element(const slot_type& slot) { return slot.value; }
	static mutable_value_type& mutable_element(slot_type& slot) { return slot.mutable_value; }
	static const mutable_value_type& mutable_element(const slot_type& slot) { return slot.mutable_value; }

	// Begins the union's lifetime, then constructs its mutable member in place.
	template <typename... Args>
	static void construct(slot_type* slot, Args&&... args) {
		slot = ::new (static_cast<void*>(slot)) slot_type;
#if __cplusplus > 201703L
		std::construct_at(std::addressof(slot->mutable_value), std::forward<Args>(args)...);
#else
		::new (static_cast<void*>(std::addressof(slot->mutable_value))) mutable_value_type(std::forward<Args>(args)...);
#endif
	}
	static void destroy(slot_type* slot) { std::destroy_at(std::addressof(slot->mutable_value)); }

	static void swap(mutable_value_type& a, mutable_value_type& b) {
		btree_swap_helper(a.first, b.first);
		btree_swap_helper(a.second, b.second);
	}

	static const Key& get_key(const value_type& x) {
		return x.first;
	}
	static const Key& get_key(const mutable_value_type& x) {
		return x.first;
	}
};

// A parameters structure for holding the type parameters for a btree_set.
template <typename Key, typename Compare, typename Alloc, int TargetNodeSize>
struct btree_set_params : public btree_common_params<Key, Compare, Alloc, TargetNodeSize, sizeof(Key)> {
	typedef std::false_type data_type;
	typedef std::false_type mapped_type;
	typedef Key value_type;
	typedef Key mutable_value_type;
	typedef value_type* pointer;
	typedef const value_type* const_pointer;
	typedef value_type& reference;
	typedef const value_type& const_reference;

	enum {
		kValueSize = sizeof(Key),
	};

	// A set slot is its key.
	typedef Key slot_type;
	static reference element(slot_type& slot) { return slot; }
	static const_reference element(const slot_type& slot) { return slot; }
	static mutable_value_type& mutable_element(slot_type& slot) { return slot; }
	static const mutable_value_type& mutable_element(const slot_type& slot) { return slot; }

	template <typename... Args>
	static void construct(slot_type* slot, Args&&... args) {
#if __cplusplus > 201703L
		std::construct_at(slot, std::forward<Args>(args)...);
#else
		::new (static_cast<void*>(slot)) slot_type(std::forward<Args>(args)...);
#endif
	}
	static void destroy(slot_type* slot) { std::destroy_at(slot); }

	static void swap(mutable_value_type& a, mutable_value_type& b) {
		btree_swap_helper(a, b);
	}

	static const Key& get_key(const value_type& x) {
		return x;
	}
};

// An adapter class that converts a lower-bound compare into an upper-bound
// compare.
template <typename Key, typename Compare>
struct btree_upper_bound_adapter : public Compare {
	btree_upper_bound_adapter(Compare c) : Compare(c) { }
	bool operator()(const Key& a, const Key& b) const {
		return !static_cast<const Compare&>(*this)(b, a);
	}
};

template <typename Key, typename CompareTo>
struct btree_upper_bound_compare_to_adapter : public CompareTo {
	btree_upper_bound_compare_to_adapter(CompareTo c) : CompareTo(c) { }
	int operator()(const Key& a, const Key& b) const {
		return static_cast<const CompareTo&>(*this)(b, a);
	}
};

// Dispatch helper class for using linear search with plain compare.
template <typename K, typename N, typename Compare>
struct btree_linear_search_plain_compare {
	static int lower_bound(const K& k, const N& n, Compare comp)  {
		return n.linear_search_plain_compare(k, 0, n.count(), comp);
	}
	static int upper_bound(const K& k, const N& n, Compare comp)  {
		typedef btree_upper_bound_adapter<K, Compare> upper_compare;
		return n.linear_search_plain_compare(k, 0, n.count(), upper_compare(comp));
	}
};

// Dispatch helper class for using linear search with compare-to
template <typename K, typename N, typename CompareTo>
struct btree_linear_search_compare_to {
	static int lower_bound(const K& k, const N& n, CompareTo comp)  {
		return n.linear_search_compare_to(k, 0, n.count(), comp);
	}
	static int upper_bound(const K& k, const N& n, CompareTo comp)  {
		typedef btree_upper_bound_adapter<K,
				btree_key_comparer<K, CompareTo, true>> upper_compare;
		return n.linear_search_plain_compare(k, 0, n.count(), upper_compare(comp));
	}
};

// Dispatch helper class for using binary search with plain compare.
template <typename K, typename N, typename Compare>
struct btree_binary_search_plain_compare {
	static int lower_bound(const K& k, const N& n, Compare comp)  {
		return n.binary_search_plain_compare(k, 0, n.count(), comp);
	}
	static int upper_bound(const K& k, const N& n, Compare comp)  {
		typedef btree_upper_bound_adapter<K, Compare> upper_compare;
		return n.binary_search_plain_compare(k, 0, n.count(), upper_compare(comp));
	}
};

// Dispatch helper class for using binary search with compare-to.
template <typename K, typename N, typename CompareTo>
struct btree_binary_search_compare_to {
	static int lower_bound(const K& k, const N& n, CompareTo /*comp*/)  {
		return n.binary_search_compare_to(k, 0, n.count(), CompareTo());
	}
	static int upper_bound(const K& k, const N& n, CompareTo comp)  {
		typedef btree_upper_bound_adapter<K,
				btree_key_comparer<K, CompareTo, true>> upper_compare;
		return n.linear_search_plain_compare(k, 0, n.count(), upper_compare(comp));
	}
};

} // namespace btree

#endif  // BTREE_BTREE_PARAMS_H__
