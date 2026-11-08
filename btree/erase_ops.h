// Copyright (c) 2013 Google Inc. and (c) 2019 Germán Méndez Bravo.
// Licensed under the Apache License, Version 2.0. See LICENSE.
#pragma once

#include "node_ops.h"

namespace btree::detail {

template <typename Node>
void remove_value(Node* node, int position) {
	auto count = node->count();
	if (node->is_leaf()) {
		for (int i = position + 1; i < count; ++i) node->swap_value(i, i - 1);
	} else {
		assert(node->child(position + 1)->count() == 0);
		for (int i = position + 1; i < count; ++i) {
			node->swap_value(i, i - 1);
			node->move_child(i + 1, i);
		}
		node->reset_child(count);
	}
	node->set_count(--count);
	node->destroy_value(count);
}

template <typename Node>
void merge(Node* node, Node* source) {
	const auto count = node->count();
	const auto source_count = source->count();
	auto parent = node->get_parent();
	const auto parent_count = parent->count();
	assert(parent == source->get_parent());
	assert(node->position() + 1 == source->position());
	parent->move_value(node->position(), node, count);
	for (int i = node->position() + 1; i < parent_count; ++i) {
		parent->move_value(i, i - 1);
		parent->move_child(i + 1, i);
	}
	parent->reset_child(parent_count);
	for (int i = 0; i < source_count; ++i) {
		source->move_value(i, node, 1 + count + i);
		if (!node->is_leaf()) source->move_child(i, node, 1 + count + i);
	}
	if (!node->is_leaf()) source->move_child(source_count, node, 1 + count + source_count);
	parent->set_count(parent_count - 1);
	node->set_count(1 + count + source_count);
	source->set_count(0);
}

template <typename Context>
void merge_nodes(Context& context, typename Context::node_type* left, typename Context::node_type* right) {
	left->merge(right);
	if (right->is_leaf() && context.rightmost() == right) context.__rightmost() = left;
	context.retire(right);
}

template <typename Context, typename Cursor>
bool try_merge_or_rebalance(Context& context, Cursor& cursor) {
	auto parent = cursor.node->get_parent();
	if (cursor.node->position() > 0) {
		auto left = parent->child(cursor.node->position() - 1);
		if (1 + left->count() + cursor.node->count() <= left->max_count()) {
			cursor.position += 1 + left->count();
			merge_nodes(context, left, cursor.node);
			cursor.node = left;
			return true;
		}
	}
	if (cursor.node->position() < parent->count()) {
		auto right = parent->child(cursor.node->position() + 1);
		if (1 + cursor.node->count() + right->count() <= right->max_count()) {
			merge_nodes(context, cursor.node, right);
			return true;
		}
		if (right->count() > Context::min_node_values && (cursor.node->count() == 0 || cursor.position > 0)) {
			auto amount = (right->count() - cursor.node->count()) / 2;
			amount = std::min(amount, right->count() - 1);
			cursor.node->rebalance_right_to_left(right, amount);
			return false;
		}
	}
	if (cursor.node->position() > 0) {
		auto left = parent->child(cursor.node->position() - 1);
		if (left->count() > Context::min_node_values && (cursor.node->count() == 0 || cursor.position < cursor.node->count())) {
			auto amount = (left->count() - cursor.node->count()) / 2;
			amount = std::min(amount, left->count() - 1);
			left->rebalance_left_to_right(cursor.node, amount);
			cursor.position += amount;
			return false;
		}
	}
	return false;
}

template <typename Context>
void shrink_root(Context& context) {
	auto root = context.root();
	if (root->count() != 0) return;
	if (root->is_leaf()) {
		context.retire(root);
		context.__root() = context.__rightmost() = nullptr;
	} else {
		auto child = root->child(0);
		if (child->is_leaf()) {
			child->make_root();
			context.retire(root);
			context.__root() = context.__rightmost() = child;
		} else {
			child->swap(root);
			context.retire(child);
		}
	}
}

template <typename Context, typename Cursor>
void erase_value(Context& context, Cursor cursor) {
	if (!cursor.node->is_leaf()) {
		auto predecessor = cursor.node->child(cursor.position);
		while (!predecessor->is_leaf()) predecessor = predecessor->child(predecessor->count());
		const int position = predecessor->count() - 1;
		cursor.node->swap_value(cursor.position, predecessor, position);
		cursor.node = predecessor;
		cursor.position = position;
	}
	cursor.node->remove_value(cursor.position);
	while (cursor.node != context.root() && cursor.node->count() < Context::min_node_values) {
		if (!try_merge_or_rebalance(context, cursor)) break;
		cursor.node = cursor.node->get_parent();
	}
	shrink_root(context);
}

} // namespace btree::detail
