// Copyright (c) 2013 Google Inc. and (c) 2019 Germán Méndez Bravo.
// Licensed under the Apache License, Version 2.0. See LICENSE.
#pragma once

#include <cassert>
#include <cstddef>
#include <memory>

namespace btree::detail {

template <typename Node>
class drain_state {
	Node* root_ = nullptr;
	Node* current_ = nullptr;
	bool after_child_ = false;
	bool root_metadata_ = false;

	template <typename Allocator>
	void release_node(Allocator& allocator) noexcept {
		auto node = current_;
		const bool is_root = node == root_;
		const auto bytes = node->is_leaf() ? Node::leaf_size(node->max_count()) :
			(is_root && root_metadata_ ? Node::root_size() : Node::internal_size());
		current_ = is_root ? nullptr : node->get_parent();
		assert(node->count() == 0);
		node->destroy();
		std::destroy_at(node);
		std::allocator_traits<Allocator>::deallocate(allocator,
			reinterpret_cast<typename Node::allocation_unit*>(node), Node::allocation_count(bytes));
		if (is_root) root_ = nullptr;
		after_child_ = true;
	}

public:
	drain_state() = default;
	drain_state(Node* root, bool metadata) noexcept : root_(root), current_(root), root_metadata_(metadata) {}
	bool done() const noexcept { return root_ == nullptr; }
	void reset() noexcept { *this = drain_state{}; }
	template <typename Allocator>
	std::size_t step(Allocator& allocator, std::size_t limit) noexcept {
		std::size_t used = 0;
		while (used < limit && !done()) {
			if (current_->is_leaf() || after_child_) {
				if (current_->count() != 0) {
					const int last = current_->count() - 1;
					current_->destroy_value(last);
					current_->set_count(last);
					after_child_ = false;
				} else release_node(allocator);
			} else {
				current_ = current_->child(current_->count());
				after_child_ = false;
			}
			++used;
		}
		return used;
	}
};

} // namespace btree::detail
