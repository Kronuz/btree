// Copyright (c) 2013 Google Inc. and (c) 2019 Germán Méndez Bravo.
// Licensed under the Apache License, Version 2.0. See LICENSE.
#pragma once

#include <algorithm>
#include <cassert>
#include <utility>

namespace btree::detail {

template <typename Node, typename... Args>
inline void insert_value(Node* node, int i, Args&&... args) {
	auto cnt = node->count();

	assert(i <= cnt);

	// Initialize new value at the end.
	node->construct_value(cnt, std::forward<Args>(args)...);

	// Move initialized value to the correct position.
	if (cnt > i) {
		if (node->is_leaf()) {
			for (int j = cnt; j > i; --j) {
				node->swap_value(j - 1, j);
			}
		} else {
			for (int j = cnt; j > i; --j) {
				node->swap_value(j - 1, j);
				node->move_child(j, j + 1);
			}
		}
	}

	// Increase number of items
	node->set_count(cnt + 1);
}

template <typename Node>
void rebalance_right_to_left(Node* node, Node* src, int to_move) {
	auto cnt = node->count();
	auto src_cnt = src->count();

	assert(node->get_parent() == src->get_parent());
	assert(node->position() + 1 == src->position());
	assert(src_cnt >= cnt);
	assert(to_move >= 1);
	assert(to_move <= src_cnt);

	// Move the delimiting value to the left node.
	node->get_parent()->move_value(node->position(), node, cnt);

	// Move the new delimiting value from the right node.
	src->move_value(to_move - 1, node->get_parent(), node->position());

	if (node->is_leaf()) {
		// Move the values from the right to the left node.
		for (int i = 1; i < to_move; ++i) {
			src->move_value(i - 1, node, cnt + i);
		}
		// Shift the values in the right node to their correct position.
		for (int i = to_move; i < src_cnt; ++i) {
			src->move_value(i, i - to_move);
		}
	} else {
		// Move the values and child pointers from the right to the left node.
		src->move_child(0, node, 1 + cnt);
		for (int i = 1; i < to_move; ++i) {
			src->move_value(i - 1, node, cnt + i);
			src->move_child(i, node, 1 + cnt + i);
		}
		// Shift the values and child pointers in the right node to their correct position.
		for (int i = to_move; i < src_cnt; ++i) {
			src->move_value(i, i - to_move);
			src->move_child(i, i - to_move);
		}
		src->move_child(src_cnt, src_cnt - to_move);
	}

	// Fixup the counts on the src and dst nodes.
	node->set_count(cnt + to_move);
	src->set_count(src_cnt - to_move);
}

template <typename Node>
void rebalance_left_to_right(Node* node, Node* dst, int to_move) {
	auto cnt = node->count();
	auto dst_cnt = dst->count();

	assert(node->get_parent() == dst->get_parent());
	assert(node->position() + 1 == dst->position());
	assert(cnt >= dst_cnt);
	assert(to_move >= 1);
	assert(to_move <= cnt);

	// Make room in the right node for the new values.
	for (int i = dst_cnt - 1; i >= 0; --i) {
		dst->move_value(i, i + to_move);
	}

	// Move the delimiting value to the right node.
	node->get_parent()->move_value(node->position(), dst, to_move - 1);

	// Move the new delimiting value from the left node.
	node->move_value(cnt - to_move, node->get_parent(), node->position());

	if (node->is_leaf()) {
		// Move the values from the left to the right node.
		for (int i = 1; i < to_move; ++i) {
			node->move_value(cnt - to_move + i, dst, i - 1);
		}
	} else {
		// Move the values and child pointers from the left to the right node.
		for (int i = dst_cnt; i >= 0; --i) {
			dst->move_child(i, i + to_move);
		}
		// Move the values and child pointers from the left to the right node.
		for (int i = 1; i < to_move; ++i) {
			node->move_value(cnt - to_move + i, dst, i - 1);
			node->move_child(cnt - to_move + i, dst, i - 1);
		}
		node->move_child(cnt, dst, to_move - 1);
	}

	// Fixup the counts on the src and dst nodes.
	node->set_count(cnt - to_move);
	dst->set_count(dst_cnt + to_move);
}

template <typename Node>
void split(Node* node, Node* dst, int insert_position) {
	auto cnt = node->count();
	auto dst_cnt = dst->count();

	assert(dst_cnt == 0);

	// Keep spare capacity on the insertion side of an edge-biased split.
	if (insert_position == 0) {
		dst_cnt = cnt - 1;
		dst->set_count(dst_cnt);
	} else if (insert_position != node->max_count()) {
		dst_cnt = cnt / 2;
		dst->set_count(dst_cnt);
	}
	assert(cnt > dst_cnt);
	cnt -= dst_cnt;
	node->set_count(cnt);

	if (node->is_leaf()) {
		// Move values from the left sibling to the right sibling.
		for (int i = 0; i < dst_cnt; ++i) {
			node->move_value(cnt + i, dst, i);
		}
	} else {
		// Move values and child pointers from the left sibling to the right sibling.
		for (int i = 0; i < dst_cnt; ++i) {
			node->move_value(cnt + i, dst, i);
			node->move_child(cnt + i, dst, i);
		}
		node->move_child(cnt + dst_cnt, dst, dst_cnt);
	}

	// The split key is the largest value in the left sibling.
	node->set_count(--cnt);
	node->get_parent()->insert_value(node->position(), std::move(node->value(cnt)));
	node->destroy_value(cnt);
	node->get_parent()->set_child(node->position() + 1, dst);
}

template <typename Node>
void swap_nodes(Node* node, Node* x) {
	auto cnt = node->count();
	auto x_cnt = x->count();

	int min = std::min(cnt, x_cnt);

	assert(node->is_leaf() == x->is_leaf());

	if (node->is_leaf()) {
		// Swap the values.
		for (int i = 0; i < min; ++i) {
			node->swap_value(i, x);
		}
		for (int i = min; i < x_cnt; ++i) {
			x->move_value(i, node);
		}
		for (int i = min; i < cnt; ++i) {
			node->move_value(i, x);
		}
	} else {
		// Swap the values and child pointers.
		for (int i = 0; i < min; ++i) {
			node->swap_value(i, x);
			node->swap_child(i, x);
		}
		node->swap_child(min, x);
		for (int i = min; i < x_cnt; ++i) {
			x->move_value(i, node);
			x->move_child(i + 1, node);
		}
		for (int i = min; i < cnt; ++i) {
			node->move_value(i, x);
			node->move_child(i + 1, x);
		}
	}

	// Swap the counts.
	node->set_count(x_cnt);
	x->set_count(cnt);
}

template <typename Context, typename Cursor>
void rebalance_or_split(Context& context, Cursor& iter) {
	using node_type = typename Context::node_type;

	node_type*& node = iter.node;
	int& insert_position = iter.position;
	assert(node->count() == node->max_count());
	node_type* split_node = nullptr;

	// First try to make room on the node by rebalancing.
	node_type* parent = node->get_parent();
	if (node != context.root()) {
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

		// Rebalancing failed, make sure there is room on the parent node for a new
		// value.
		if (parent->count() == parent->max_count()) {
			Cursor parent_iter(node->get_parent(), node->position());
			rebalance_or_split(context, parent_iter);
		}
	} else {
		// Rebalancing not possible because this is the root node.
		if (context.root()->is_leaf()) {
			// The root node is currently a leaf node: create a new root node and set
			// the current root node as the child of the new root.
			parent = context.new_internal_root_node();
			try {
				split_node = context.new_leaf_node(parent);
			} catch (...) {
				context.delete_unpublished_root(parent);
				throw;
			}
			parent->set_child(0, context.root());
			context.__root() = parent;
			assert(context.__rightmost() == parent->child(0));
		} else {
			// The root node is an internal node. We do not want to create a new root
			// node because the root node is special and holds the size of the tree
			// and a pointer to the rightmost node. So we create a new internal node
			// and move all of the items on the current root into the new node.
			parent = context.new_internal_node(parent);
			parent->set_child(0, parent);
			parent->swap(context.root());
			node = parent;
		}
	}

	// Split the node.
	if (node->is_leaf()) {
		if (!split_node) split_node = context.new_leaf_node(parent);
		node->split(split_node, insert_position);
		if (context.rightmost() == node) {
			context.__rightmost() = split_node;
		}
	} else {
		split_node = context.new_internal_node(parent);
		node->split(split_node, insert_position);
	}

	if (insert_position > node->count()) {
		insert_position = insert_position - node->count() - 1;
		node = split_node;
	}
}

} // namespace btree::detail
