/*
 * Copyright (c) 2019 German Mendez Bravo (Kronuz)
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
 *
 * A btree implementation of the STL set and map interfaces. A btree is both
 * smaller and faster than STL set/map. The red-black tree implementation of
 * STL set/map has an overhead of 3 pointers (left, right and parent) plus the
 * node color information for each stored value. So a set<int32> consumes 20
 * bytes for each value stored. This btree implementation stores multiple
 * values on fixed size nodes (usually 256 bytes) and doesn't store child
 * pointers for leaf nodes. The result is that a btree::set<int32> may use much
 * less memory per stored value. For the random insertion benchmark in
 * btree_test.cc, a btree::set<int32> with node-size of 256 uses 4.9 bytes per
 * stored value.
 *
 * The packing of multiple values on to each node of a btree has another effect
 * besides better space utilization: better cache locality due to fewer cache
 * lines being accessed. Better cache locality translates into faster
 * operations.
 *
 * CAVEATS
 *
 * Insertions and deletions on a btree can cause splitting, merging or
 * rebalancing of btree nodes. And even without these operations, insertions
 * and deletions on a btree will move values around within a node. In both
 * cases, the result is that insertions and deletions can invalidate iterators
 * pointing to values other than the one being inserted/deleted. This is
 * notably different from STL set/map which takes care to not invalidate
 * iterators on insert/erase except, of course, for iterators pointing to the
 * value being erased.  A partial workaround when erasing is available:
 * erase() returns an iterator pointing to the item just after the one that was
 * erased (or end() if none exists).  See also safe_btree.
 *
 * PERFORMANCE
 *
 *   btree_bench --benchmarks=. 2>&1 | ./benchmarks.awk
 *
 * Run on pmattis-warp.nyc (4 X 2200 MHz CPUs); 2010/03/04-15:23:06
 * Benchmark                 STL(ns) B-Tree(ns) @    <size>
 * --------------------------------------------------------
 * BM_set_int32_insert        1516      608  +59.89%  <256>    [40.0,  5.2]
 * BM_set_int32_lookup        1160      414  +64.31%  <256>    [40.0,  5.2]
 * BM_set_int32_fulllookup     960      410  +57.29%  <256>    [40.0,  4.4]
 * BM_set_int32_delete        1741      528  +69.67%  <256>    [40.0,  5.2]
 * BM_set_int32_queueaddrem   3078     1046  +66.02%  <256>    [40.0,  5.5]
 * BM_set_int32_mixedaddrem   3600     1384  +61.56%  <256>    [40.0,  5.3]
 * BM_set_int32_fifo           227      113  +50.22%  <256>    [40.0,  4.4]
 * BM_set_int32_fwditer        158       26  +83.54%  <256>    [40.0,  5.2]
 * BM_map_int32_insert        1551      636  +58.99%  <256>    [48.0, 10.5]
 * BM_map_int32_lookup        1200      508  +57.67%  <256>    [48.0, 10.5]
 * BM_map_int32_fulllookup     989      487  +50.76%  <256>    [48.0,  8.8]
 * BM_map_int32_delete        1794      628  +64.99%  <256>    [48.0, 10.5]
 * BM_map_int32_queueaddrem   3189     1266  +60.30%  <256>    [48.0, 11.6]
 * BM_map_int32_mixedaddrem   3822     1623  +57.54%  <256>    [48.0, 10.9]
 * BM_map_int32_fifo           151      134  +11.26%  <256>    [48.0,  8.8]
 * BM_map_int32_fwditer        161       32  +80.12%  <256>    [48.0, 10.5]
 * BM_set_int64_insert        1546      636  +58.86%  <256>    [40.0, 10.5]
 * BM_set_int64_lookup        1200      512  +57.33%  <256>    [40.0, 10.5]
 * BM_set_int64_fulllookup     971      487  +49.85%  <256>    [40.0,  8.8]
 * BM_set_int64_delete        1745      616  +64.70%  <256>    [40.0, 10.5]
 * BM_set_int64_queueaddrem   3163     1195  +62.22%  <256>    [40.0, 11.6]
 * BM_set_int64_mixedaddrem   3760     1564  +58.40%  <256>    [40.0, 10.9]
 * BM_set_int64_fifo           146      103  +29.45%  <256>    [40.0,  8.8]
 * BM_set_int64_fwditer        162       31  +80.86%  <256>    [40.0, 10.5]
 * BM_map_int64_insert        1551      720  +53.58%  <256>    [48.0, 20.7]
 * BM_map_int64_lookup        1214      612  +49.59%  <256>    [48.0, 20.7]
 * BM_map_int64_fulllookup     994      592  +40.44%  <256>    [48.0, 17.2]
 * BM_map_int64_delete        1778      764  +57.03%  <256>    [48.0, 20.7]
 * BM_map_int64_queueaddrem   3189     1547  +51.49%  <256>    [48.0, 20.9]
 * BM_map_int64_mixedaddrem   3779     1887  +50.07%  <256>    [48.0, 21.6]
 * BM_map_int64_fifo           147      145   +1.36%  <256>    [48.0, 17.2]
 * BM_map_int64_fwditer        162       41  +74.69%  <256>    [48.0, 20.7]
 * BM_set_string_insert       1989     1966   +1.16%  <256>    [64.0, 44.5]
 * BM_set_string_lookup       1709     1600   +6.38%  <256>    [64.0, 44.5]
 * BM_set_string_fulllookup   1573     1529   +2.80%  <256>    [64.0, 35.4]
 * BM_set_string_delete       2520     1920  +23.81%  <256>    [64.0, 44.5]
 * BM_set_string_queueaddrem  4706     4309   +8.44%  <256>    [64.0, 48.3]
 * BM_set_string_mixedaddrem  5080     4654   +8.39%  <256>    [64.0, 46.7]
 * BM_set_string_fifo          318      512  -61.01%  <256>    [64.0, 35.4]
 * BM_set_string_fwditer       182       93  +48.90%  <256>    [64.0, 44.5]
 * BM_map_string_insert       2600     2227  +14.35%  <256>    [72.0, 55.8]
 * BM_map_string_lookup       2068     1730  +16.34%  <256>    [72.0, 55.8]
 * BM_map_string_fulllookup   1859     1618  +12.96%  <256>    [72.0, 44.0]
 * BM_map_string_delete       3168     2080  +34.34%  <256>    [72.0, 55.8]
 * BM_map_string_queueaddrem  5840     4701  +19.50%  <256>    [72.0, 59.4]
 * BM_map_string_mixedaddrem  6400     5200  +18.75%  <256>    [72.0, 57.8]
 * BM_map_string_fifo          398      596  -49.75%  <256>    [72.0, 44.0]
 * BM_map_string_fwditer       243      113  +53.50%  <256>    [72.0, 55.8]
 */

#ifndef BTREE_BTREE_H__
#define BTREE_BTREE_H__

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <ostream>
#include <string>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "node_ops.h"
#include "drain_state.h"
#include "mutation_images.h"

namespace btree {

// Types small_ and big_ are promise that sizeof(small_) < sizeof(big_)
typedef char small_;

struct big_ {
	char dummy[2];
};

struct btree_extract_key_fail_tag { };
struct btree_extract_key_self_tag { };
struct btree_extract_key_first_tag { };

template <typename ValueType, typename Key,
		  typename RawValueType = typename std::remove_const<typename std::remove_reference<ValueType>::type>::type>
struct btree_can_extract_key
	: std::conditional<std::is_same<RawValueType, Key>::value,
		btree_extract_key_self_tag,
		btree_extract_key_fail_tag>::type { };

template <typename _Pair, typename Key, typename First, typename Second>
struct btree_can_extract_key<_Pair, Key, std::pair<First, Second>>
	: std::conditional<std::is_same<typename std::remove_const<First>::type, Key>::value,
		btree_extract_key_first_tag,
		btree_extract_key_fail_tag>::type { };

// btree_can_extract_map_key uses true_type/false_type instead of the tags.
// It returns true if Key != ContainerValueType (the container is a map not a set)
// and ValueType == Key.
template <typename ValueType, typename Key, typename ContainerValueType,
		  typename RawValueType = typename std::remove_const<typename std::remove_reference<ValueType>::type>::type>
struct btree_can_extract_map_key : std::integral_constant<bool, std::is_same<RawValueType, Key>::value> { };

// This specialization returns btree_extract_key_fail_tag for non-map containers
// because Key == ContainerValueType
template <typename ValueType, typename Key, typename RawValueType>
struct btree_can_extract_map_key<ValueType, Key, Key, RawValueType> : std::false_type { };

// A helper type used to indicate that a key-compare-to functor has been
// provided. A user can specify a key-compare-to functor by doing:
//
//  struct MyStringComparer
//      : public util::btree::btree_key_compare_to_tag {
//    int operator()(const string& a, const string& b) const {
//      return a.compare(b);
//    }
//  };
//
// Note that the return type is an int and not a bool. There is a
// static_assert which enforces this return type.
struct btree_key_compare_to_tag { };

// A helper class that indicates if the Compare parameter is derived from
// btree_key_compare_to_tag.
template <typename Compare>
struct btree_is_key_compare_to : public std::is_convertible<Compare, btree_key_compare_to_tag> { };

// Detects a comparator with a nested `is_transparent` type (e.g.
// std::less<>), matching std::map/std::set's own heterogeneous-lookup
// convention.
template <typename Compare, typename = void>
struct btree_is_transparent : std::false_type { };
template <typename Compare>
struct btree_is_transparent<Compare, std::void_t<typename Compare::is_transparent>> : std::true_type { };

// A helper class to convert a boolean comparison into a three-way
// "compare-to" comparison that returns a negative value to indicate
// less-than, zero to indicate equality and a positive value to
// indicate greater-than. This helper class is specialized for
// less<string> and greater<string>. The btree_key_compare_to_adapter
// class is provided so that btree users automatically get the more
// efficient compare-to code when using common google string types
// with common comparison functors.
template <typename Compare>
struct btree_key_compare_to_adapter : Compare {
	btree_key_compare_to_adapter() = default;
	btree_key_compare_to_adapter(const Compare& c) noexcept(std::is_nothrow_copy_constructible<Compare>::value) : Compare(c) { }
	btree_key_compare_to_adapter(const btree_key_compare_to_adapter<Compare>&) = default;
};

template <>
struct btree_key_compare_to_adapter<std::less<std::string>> : public btree_key_compare_to_tag {
	btree_key_compare_to_adapter() noexcept { }
	btree_key_compare_to_adapter(const std::less<std::string>&) noexcept { }
	btree_key_compare_to_adapter(const btree_key_compare_to_adapter<std::less<std::string>>&) noexcept { }
	int operator()(const std::string& a, const std::string& b) const {
		return a.compare(b);
	}
};

template <>
struct btree_key_compare_to_adapter<std::greater<std::string>> : public btree_key_compare_to_tag {
	btree_key_compare_to_adapter() noexcept { }
	btree_key_compare_to_adapter(const std::greater<std::string>&) noexcept { }
	btree_key_compare_to_adapter(const btree_key_compare_to_adapter<std::greater<std::string>>&) noexcept { }
	int operator()(const std::string& a, const std::string& b) const {
		return b.compare(a);
	}
};

// A helper class that allows a compare-to functor to behave like a plain
// compare functor. This specialization is used when we do not have a
// compare-to functor.
template <typename Key, typename Compare, bool HaveCompareTo>
struct btree_key_comparer {
	btree_key_comparer() { }
	btree_key_comparer(Compare c) : comp(c) { }
	static bool bool_compare(const Compare& comp, const Key& x, const Key& y) {
		return comp(x, y);
	}
	bool operator()(const Key& x, const Key& y) const {
		return bool_compare(comp, x, y);
	}
	Compare comp;
};

// A specialization of btree_key_comparer when a compare-to functor is
// present. We need a plain (boolean) comparison in some parts of the btree
// code, such as insert-with-hint.
template <typename Key, typename Compare>
struct btree_key_comparer<Key, Compare, true> {
	btree_key_comparer() { }
	btree_key_comparer(Compare c) : comp(c) { }
	static bool bool_compare(const Compare& comp, const Key& x, const Key& y) {
		return comp(x, y) < 0;
	}
	bool operator()(const Key& x, const Key& y) const {
		return bool_compare(comp, x, y);
	}
	Compare comp;
};

// A helper function to compare to keys using the specified compare
// functor. This dispatches to the appropriate btree_key_comparer comparison,
// depending on whether we have a compare-to functor or not (which depends on
// whether Compare is derived from btree_key_compare_to_tag).
template <typename Key, typename Compare>
static bool btree_compare_keys(const Compare& comp, const Key& x, const Key& y) {
	typedef btree_key_comparer<Key, Compare,
			btree_is_key_compare_to<Compare>::value> key_comparer;
	return key_comparer::bool_compare(comp, x, y);
}

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

// A parameters structure for holding the type parameters for a map.
template <typename Key, typename Data, typename Compare, typename Alloc, int TargetNodeSize>
struct btree_map_params : public btree_common_params<Key, Compare, Alloc, TargetNodeSize, sizeof(Key) + sizeof(Data)> {
	typedef Data data_type;
	typedef Data mapped_type;
	typedef std::pair<const Key, data_type> value_type;
	typedef value_type* pointer;
	typedef const value_type* const_pointer;
	typedef value_type& reference;
	typedef const value_type& const_reference;

	enum {
		kValueSize = sizeof(Key) + sizeof(data_type),
	};

	static const Key& get_key(const value_type& x) {
		return x.first;
	}
};

// A parameters structure for holding the type parameters for a btree_set.
template <typename Key, typename Compare, typename Alloc, int TargetNodeSize>
struct btree_set_params : public btree_common_params<Key, Compare, Alloc, TargetNodeSize, sizeof(Key)> {
	typedef std::false_type data_type;
	typedef std::false_type mapped_type;
	typedef Key value_type;
	typedef value_type* pointer;
	typedef const value_type* const_pointer;
	typedef value_type& reference;
	typedef const value_type& const_reference;

	enum {
		kValueSize = sizeof(Key),
	};

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
		return reinterpret_cast<reference>(slot_value(i));
	}
	const_reference value(int i) const {
		return reinterpret_cast<const_reference>(slot_value(i));
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

	// Appends a value after all live slots.
	template <typename... Args>
	void insert_value(int i, Args&&... args);

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
		return init_root(storage, parent, parent->count());
	}
	static btree_node* init_root(void* storage, btree_node* parent, size_type size) {
		btree_node* n = init_internal(storage, parent);
		::new (static_cast<void*>(n->storage() + root_offset())) root_metadata{parent, size};
		return n;
	}

	void destroy() {
		for (int i = 0; i < count(); ++i) {
			destroy_value(i);
		}
	}

private:
	template <typename> friend class btree;
	template <typename> friend class detail::drain_state;
	template <typename N, typename... Args> friend void detail::insert_value(N*, int, Args&&...);
	template <typename N> friend void detail::split(N*, N*, int);
	btree_node(btree_node* parent, int capacity)
		: fields_{true, 0, static_cast<typename base_fields::field_type>(capacity), 0, parent} {}
	char* storage() { return reinterpret_cast<char*>(this); }
	const char* storage() const { return reinterpret_cast<const char*>(this); }
	value_type* value_slot(int i) {
		return reinterpret_cast<value_type*>(storage() + values_offset() + std::size_t(i) * sizeof(value_type));
	}
	const value_type* value_slot(int i) const {
		return reinterpret_cast<const value_type*>(storage() + values_offset() + std::size_t(i) * sizeof(value_type));
	}
	value_type& slot_value(int i) { return *std::launder(value_slot(i)); }
	const value_type& slot_value(int i) const { return *std::launder(value_slot(i)); }
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
	void construct_value(value_type* v, Args&&... args) {
#if __cplusplus > 201703L
		std::construct_at(v, std::forward<Args>(args)...);
#else
		new (v) value_type(std::forward<Args>(args)...);
#endif
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

	void destroy_value(value_type* v) {
		std::destroy_at(v);
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

template <typename Node, typename Reference, typename Pointer>
class btree_iterator {
public:
	typedef typename Node::key_type key_type;
	typedef typename Node::size_type size_type;
	typedef typename Node::difference_type difference_type;
	typedef typename Node::params_type params_type;

	typedef Node node_type;
	typedef typename std::remove_const<Node>::type normal_node;
	typedef const Node const_node;
	typedef typename params_type::value_type value_type;
	typedef typename params_type::pointer normal_pointer;
	typedef typename params_type::reference normal_reference;
	typedef typename params_type::const_pointer const_pointer;
	typedef typename params_type::const_reference const_reference;

	typedef Pointer pointer;
	typedef Reference reference;
	typedef std::bidirectional_iterator_tag iterator_category;

	typedef btree_iterator<normal_node, normal_reference, normal_pointer> iterator;
	typedef btree_iterator<const_node, const_reference, const_pointer> const_iterator;
	typedef btree_iterator<Node, Reference, Pointer> self_type;

	btree_iterator()
		: node(nullptr),
		  position(-1) {
	}
	btree_iterator(const iterator& x)
		: node(x.node),
		  position(x.position) {
	}

	bool operator==(const const_iterator& x) const {
		return node == x.node && position == x.position;
	}
	bool operator!=(const const_iterator& x) const {
		return node != x.node || position != x.position;
	}
	bool operator==(const iterator& x) const {
		return node == x.node && position == x.position;
	}
	bool operator!=(const iterator& x) const {
		return node != x.node || position != x.position;
	}

	// Accessors for the key/value the iterator is pointing at.
	const key_type& key() const {
		return node->key(position);
	}
	reference operator*() const {
		return node->value(position);
	}
	pointer operator->() const {
		return &node->value(position);
	}

	self_type& operator++() {
		increment();
		return *this;
	}
	self_type& operator--() {
		decrement();
		return *this;
	}
	self_type operator++(int) {
		self_type tmp =* this;
		++*this;
		return tmp;
	}
	self_type operator--(int) {
		self_type tmp =* this;
		--*this;
		return tmp;
	}

private:
	btree_iterator(Node* n, int p)
		: node(const_cast<normal_node*>(n)),
		  position(p) {
	}

	// Increment/decrement the iterator.
	void increment() {
		if (node->is_leaf() && ++position < node->count()) {
			return;
		}
		increment_slow();
	}
	void increment_by(int count);
	void increment_slow();

	void decrement() {
		if (node->is_leaf() && --position >= 0) {
			return;
		}
		decrement_slow();
	}
	void decrement_slow();

	// The node in the tree the iterator is pointing at.
	normal_node* node;
	// The position within the node of the tree the iterator is pointing at.
	int position;

	template <typename> friend class btree;
	template <typename Context, typename Cursor> friend void detail::rebalance_or_split(Context&, Cursor&);
	friend iterator;
	friend const_iterator;
};

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
	class drain_cursor;
	using drain_type = drain_cursor;
	drain_type drain() noexcept(std::is_nothrow_copy_constructible<internal_allocator_type>::value);

	// Default constructor.
	btree(const key_compare& comp, const allocator_type& alloc);

	// Copy constructor.
	btree(const self_type& x);
	btree(self_type&& x) noexcept(std::is_nothrow_copy_constructible<key_compare>::value &&
		std::is_nothrow_copy_constructible<internal_allocator_type>::value);

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
		value_type v(std::forward<Args>(args)...);
		return emplace_unique_key_args(params_type::get_key(v), std::move(v));
	}

	template <class P>
	std::pair<iterator, bool>
	emplace_unique_extract_key(P&& x, btree_extract_key_fail_tag) {
		value_type v(std::forward<P>(x));
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
	iterator emplace_hint_unique(const_iterator hint, P&& x) {
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
		value_type v(std::forward<Args>(args)...);
		return emplace_hint_unique_key_args(hint, params_type::get_key(v), std::move(v));
	}

	template <class P>
	iterator
	emplace_hint_unique_extract_key(const_iterator hint, P&& x, btree_extract_key_fail_tag) {
		value_type v(std::forward<P>(x));
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
		value_type v(std::forward<Args>(args)...);
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
		value_type v(std::forward<Args>(args)...);
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
		return std::distance(lower_bound(key), upper_bound(key));
	}

	// Clear the btree, deleting all of the values it contains.
	void clear();

	// Swap the contents of* this and x.
	void swap(self_type& x);

	// Assign the contents of x to* this.
	self_type& operator=(const self_type& x) {
		if (&x == this) {
			// Don't copy onto ourselves.
			return *this;
		}
		assign(x);
		return *this;
	}

	self_type& operator=(self_type&& x) noexcept(std::is_nothrow_copy_assignable<key_compare>::value &&
		std::is_nothrow_copy_assignable<internal_allocator_type>::value) {
		if (&x == this) return *this;
		clear();
		__key_comp() = x.key_comp();
		__internal_allocator() = x.internal_allocator();
		__root() = x.root();
		x.__root() = nullptr;
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
	node_type* new_leaf_root_node(int max_count) {
		void* storage = allocate_node(node_type::leaf_size(max_count));
		return node_type::init_leaf(storage, static_cast<node_type*>(storage), max_count);
	}
	void delete_leaf_node(node_type* node) {
		deallocate_node(node, node_type::leaf_size(node->max_count()));
	}

	struct copy_level {
		size_type nodes;
		size_type next = 0;
	};
	using copy_level_allocator = typename internal_allocator_traits::template rebind_alloc<copy_level>;
	using copy_plan = std::vector<copy_level, copy_level_allocator>;

	node_type* build_ordered_copy(const_iterator& source, copy_plan& levels, std::size_t height,
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
			result = root_node ? node_type::init_root(allocate_node(node_type::root_size()), parent, 0)
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
		copy_plan levels{copy_level_allocator(internal_allocator())};
		const auto leaves = size / (kNodeValues + 1) + 1;
		levels.push_back({leaves});
		while (levels.back().nodes > 1)
			levels.push_back({1 + (levels.back().nodes - 1) / (kNodeValues + 1)});
		auto iterator = source.begin();
		auto result = build_ordered_copy(iterator, levels, levels.size() - 1, size - leaves + 1, nullptr, true);
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
	class mutation_transaction;
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
	static_assert(sizeof(key_compare_checker(std::declval<key_compare>()(
		std::declval<key_type>(), std::declval<key_type>()))) == sizeof(big_),
		"key_comparison_function_must_return_bool");

	// Note: We insist on kTargetValues, which is computed from
	// Params::kTargetNodeSize, must fit the base_fields::field_type.
	static_assert((kNodeValues < (1 << (8 * sizeof(typename base_fields::field_type)))),
		"target_node_size_too_large");

	// Test the assumption made in setting kNodeValueSpace.
	static_assert(sizeof(base_fields) >= 2 * sizeof(void*),
		"node_space_assumption_incorrect");
};

} // namespace btree
#include "mutation_transaction.h"
#include "drain_cursor.h"
namespace btree {

////
// btree_node methods
template <typename P> template <typename... Args>
inline void btree_node<P>::insert_value(int i, Args&&... args) {
	assert(i == count() && i < max_count());
	construct_value(i, std::forward<Args>(args)...);
	set_count(i + 1);
}

////
// btree_iterator methods
template <typename N, typename R, typename P>
void btree_iterator<N, R, P>::increment_slow() {
	if (node->is_leaf()) {
		assert(position >= node->count());
		self_type save(*this);
		while (position == node->count() && !node->is_root()) {
			assert(node->get_parent()->child(node->position()) == node);
			position = node->position();
			node = node->get_parent();
		}
		if (position == node->count()) {
			*this = save;
		}
	} else {
		assert(position < node->count());
		node = node->child(position + 1);
		while (!node->is_leaf()) {
			node = node->child(0);
		}
		position = 0;
	}
}

template <typename N, typename R, typename P>
void btree_iterator<N, R, P>::increment_by(int count) {
	while (count > 0) {
		if (node->is_leaf()) {
			int rest = node->count() - position;
			position += std::min(rest, count);
			count = count - rest;
			if (position < node->count()) {
				return;
			}
		} else {
			--count;
		}
		increment_slow();
	}
}

template <typename N, typename R, typename P>
void btree_iterator<N, R, P>::decrement_slow() {
	if (node->is_leaf()) {
		assert(position <= -1);
		self_type save(*this);
		while (position < 0 && !node->is_root()) {
			assert(node->get_parent()->child(node->position()) == node);
			position = node->position() - 1;
			node = node->get_parent();
		}
		if (position < 0) {
			*this = save;
		}
	} else {
		assert(position >= 0);
		node = node->child(position);
		while (!node->is_leaf()) {
			node = node->child(node->count());
		}
		position = node->count() - 1;
	}
}

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

template <typename P>
btree<P>::btree(self_type&& x) noexcept(std::is_nothrow_copy_constructible<key_compare>::value &&
	std::is_nothrow_copy_constructible<internal_allocator_type>::value)
	: key_compare(x.key_comp()), root_(x.internal_allocator(), x.root()) {
	x.__root() = nullptr;
}

template <typename P> template <typename... Args>
std::pair<typename btree<P>::iterator, bool>
btree<P>::emplace_unique_key_args(const key_type& key, Args&&... args) {
	if (root() == nullptr) {
		__root() = new_leaf_root_node(1);
		return std::make_pair(internal_emplace(iterator(root(), 0), std::forward<Args>(args)...), true);
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
	if (root() == nullptr) {
		__root() = new_leaf_root_node(1);
		return internal_emplace(iterator(root(), 0), std::forward<Args>(args)...);
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
	mutation_transaction transaction(*this);
	return transaction.erase(iterator(iter.node, iter.position));
}

template <typename P>
int btree<P>::erase(const_iterator begin, const_iterator end) {
	const auto length = std::distance(begin, end);
	if (length > std::numeric_limits<int>::max()) throw std::length_error("erase range exceeds return count");
	const auto count = static_cast<int>(length);
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
btree<P>::internal_emplace(const_iterator hint, Args&&... args) {
	iterator iter(hint.node, hint.position);
	if (!iter.node->is_leaf()) {
		// We can't insert on an internal node. Instead, we'll insert after the
		// previous value which is guaranteed to be on a leaf node.
		--iter;
		++iter.position;
	}
	if (iter.position != iter.node->count() || iter.node->count() == iter.node->max_count()) {
		mutation_transaction transaction(*this, std::in_place, std::forward<Args>(args)...);
		return transaction.insert(iter);
	}
	try {
		iter.node->insert_value(iter.position, std::forward<Args>(args)...);
	} catch (...) {
		if (root()->is_leaf() && root()->count() == 0) {
			delete_leaf_node(root());
			__root() = nullptr;
		}
		throw;
	}
	if (!root()->is_leaf()) ++__size();
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
	detail::drain_state<node_type> state(node, node == root() && !node->is_leaf());
	while (!state.done()) state.step(__internal_allocator(), std::numeric_limits<std::size_t>::max());
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
	using drain_type = typename Tree::drain_type;
	drain_type drain() noexcept(noexcept(__tree.drain())) { return __tree.drain(); }

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
	iterator insert(const_iterator hint, P&& x) {
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
	iterator emplace(Args&&... args) {
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

#endif  // BTREE_BTREE_H__
