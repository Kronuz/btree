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
 * Key comparison helpers: swap helper, key extraction tags and compare-to adapters.
 * Internal: included only by btree/btree.h.
 */

#ifndef BTREE_BTREE_COMPARE_H__
#define BTREE_BTREE_COMPARE_H__

namespace btree {

// Inside a btree method, if we just call swap(), it will choose the
// btree::swap method, which we don't want. And we can't say ::swap
// because then MSVC won't pickup any std::swap() implementations. We
// can't just use std::swap() directly because then we don't get the
// specialization for types outside the std namespace. So the solution
// is to have a special swap helper function whose name doesn't
// collide with other swap functions defined by the btree classes.
template <typename T>
inline void btree_swap_helper(T& a, T& b) {
	using std::swap;
	swap(a, b);
}

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
	btree_key_compare_to_adapter() noexcept(std::is_nothrow_default_constructible<Compare>::value) { }
	btree_key_compare_to_adapter(const Compare& c) noexcept(noexcept(Compare(c))) : Compare(c) { }
	btree_key_compare_to_adapter(const btree_key_compare_to_adapter<Compare>& c) noexcept(noexcept(Compare(c)))
		: Compare(c) {
	}
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

} // namespace btree

#endif  // BTREE_BTREE_COMPARE_H__
