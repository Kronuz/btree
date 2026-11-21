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
 * Out-of-class definitions of btree_node and btree_iterator methods.
 * A real header, but the pieces are meant to be used through btree/btree.h.
 */

#ifndef BTREE_BTREE_NODE_IMPL_H__
#define BTREE_BTREE_NODE_IMPL_H__

#include <assert.h>
#include <algorithm>
#include <utility>

#include "btree_node.h"
#include "btree_iterator.h"

namespace btree {

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

} // namespace btree

#endif  // BTREE_BTREE_NODE_IMPL_H__
