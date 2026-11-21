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
 * Out-of-class definitions of btree internals: emplacement, search, clear, dump, verify.
 * A real header, but the pieces are meant to be used through btree/btree.h.
 */

#ifndef BTREE_BTREE_TREE_INTERNAL_H__
#define BTREE_BTREE_TREE_INTERNAL_H__

#include <assert.h>
#include <algorithm>
#include <ostream>
#include <type_traits>
#include <utility>

#include "btree_tree.h"
#include "btree_node_impl.h"

namespace btree {

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

#endif  // BTREE_BTREE_TREE_INTERNAL_H__
