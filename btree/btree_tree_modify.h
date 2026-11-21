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
 * Out-of-class definitions of btree modification: emplace, erase, rebalance, shrink.
 * A real header, but the pieces are meant to be used through btree/btree.h.
 */

#ifndef BTREE_BTREE_TREE_MODIFY_H__
#define BTREE_BTREE_TREE_MODIFY_H__

#include <assert.h>
#include <algorithm>
#include <utility>

#include "btree_tree.h"
#include "btree_node_impl.h"

namespace btree {

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

} // namespace btree

#endif  // BTREE_BTREE_TREE_MODIFY_H__
