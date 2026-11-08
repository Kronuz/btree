// Copyright (c) 2013 Google Inc. and (c) 2019 Germán Méndez Bravo.
// Licensed under the Apache License, Version 2.0. See LICENSE.
#pragma once

#include "node_ops.h"

#include <cassert>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace btree::detail {

template <typename NativeNode, typename Allocator>
class insertion_images {
public:
	struct slot_id {
		NativeNode* source = nullptr;
		int position = -1;
		bool engaged() const noexcept { return position >= 0; }
		bool operator==(const slot_id& other) const noexcept {
			return source == other.source && position == other.position;
		}
		bool operator!=(const slot_id& other) const noexcept { return !(*this == other); }
	};
	struct image;
	struct edge {
		NativeNode* original = nullptr;
		image* staged = nullptr;
		image* resolve(insertion_images& owner) {
			if (!staged && original) staged = owner.import_native(original);
			return staged;
		}
	};
	template <typename T>
	using rebound = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
	using slots_type = std::vector<slot_id, rebound<slot_id>>;
	using edges_type = std::vector<edge, rebound<edge>>;
	enum class allocation_kind { leaf, internal, root };

	struct image {
		insertion_images& owner;
		NativeNode* original;
		NativeNode* destination;
		allocation_kind kind;
		int capacity;
		int original_count;
		int planned_count;
		int planned_position;
		edge parent;
		std::optional<slots_type> slots;
		std::optional<edges_type> edges;
		bool header_changed = false;
		bool values_changed = false;
		bool children_changed = false;
		bool retired = false;

		image(insertion_images& context, NativeNode* source)
			: owner(context), original(source), destination(source),
			  kind(source->is_leaf() ? allocation_kind::leaf :
				(source->is_root() ? allocation_kind::root : allocation_kind::internal)),
			  capacity(source->max_count()), original_count(source->count()), planned_count(source->count()),
			  planned_position(source->position()), parent{source->get_parent(), nullptr} {}

		image(insertion_images& context, allocation_kind desired, image* ancestor, int cap)
			: owner(context), original(nullptr), destination(nullptr), kind(desired), capacity(cap), original_count(0),
			  planned_count(0), planned_position(0), parent{nullptr, ancestor}, header_changed(true) {}

		bool is_leaf() const noexcept { return kind == allocation_kind::leaf; }
		bool is_root() const noexcept { return owner.root_ == this; }
		int count() const noexcept { return planned_count; }
		int max_count() const noexcept { return capacity; }
		int position() const noexcept { return planned_position; }
		image* get_parent() { return parent.resolve(owner); }
		void set_count(int value) noexcept {
			assert(value >= 0 && value <= capacity);
			planned_count = value;
			header_changed = true;
		}

		slots_type& value_slots() {
			if (!slots) {
				slots.emplace(rebound<slot_id>(owner.allocator_));
				slots->reserve(capacity);
				for (int i = 0; i < capacity; ++i)
					slots->push_back(original && i < original_count ? slot_id{original, i} : slot_id{});
			}
			return *slots;
		}
		edges_type& child_slots() {
			assert(!is_leaf());
			if (!edges) {
				edges.emplace(rebound<edge>(owner.allocator_));
				edges->resize(capacity + 1);
				if (original) for (int i = 0; i <= original_count; ++i) (*edges)[i].original = original->child(i);
			}
			return *edges;
		}
		slot_id& value(int i) {
			assert(i >= 0 && i < capacity);
			return value_slots()[i];
		}
		void construct_value(int i, slot_id id) {
			assert(id.engaged() && !value(i).engaged());
			value(i) = id;
			values_changed = true;
		}
		void destroy_value(int i) {
			assert(value(i).engaged());
			value(i) = {};
			values_changed = true;
		}
		void swap_value(int i, int j) {
			assert(i != j);
			std::swap(value(i), value(j));
			values_changed = true;
		}
		void swap_value(int i, image* other) {
			assert(other != this);
			std::swap(value(i), other->value(i));
			values_changed = other->values_changed = true;
		}
		void move_value(int i, image* destination, int j) {
			assert(destination != this || i != j);
			destination->construct_value(j, value(i));
			destroy_value(i);
		}
		void move_value(int i, image* destination) { move_value(i, destination, i); }
		void move_value(int i, int j) { move_value(i, this, j); }

		image* child(int i) {
			assert(i >= 0 && i <= capacity);
			return child_slots()[i].resolve(owner);
		}
		void set_child(int i, image* descendant) {
			assert(descendant && i >= 0 && i <= capacity);
			child_slots()[i] = {nullptr, descendant};
			descendant->parent = {nullptr, this};
			descendant->planned_position = i;
			descendant->header_changed = true;
			children_changed = true;
		}
		void reset_child(int) noexcept {}
		void swap_child(int i, image* other, int j) {
			auto left = child(i);
			auto right = other->child(j);
			assert(left && right);
			set_child(i, right);
			other->set_child(j, left);
		}
		void swap_child(int i, image* other) { swap_child(i, other, i); }
		void swap_child(int i, int j) { swap_child(i, this, j); }
		void move_child(int i, image* destination, int j) { destination->set_child(j, child(i)); }
		void move_child(int i, image* destination) { move_child(i, destination, i); }
		void move_child(int i, int j) { move_child(i, this, j); }

		void insert_value(int i, slot_id id) { ::btree::detail::insert_value(this, i, id); }
		void rebalance_right_to_left(image* source, int amount) {
			::btree::detail::rebalance_right_to_left(this, source, amount);
		}
		void rebalance_left_to_right(image* destination, int amount) {
			::btree::detail::rebalance_left_to_right(this, destination, amount);
		}
		void split(image* destination, int i) { ::btree::detail::split(this, destination, i); }
		void swap(image* other) { ::btree::detail::swap_nodes(this, other); }
	};

	using node_type = image;
	struct cursor {
		image* node;
		int position;
		cursor(image* value, int offset) : node(value), position(offset) {}
	};

	insertion_images(NativeNode* source, const Allocator& allocator)
		: allocator_(allocator), images_(rebound<image>(allocator_)),
		  by_source_(rebound<index_value>(allocator_)) {
		root_ = import_native(source);
		if (source) rightmost_ = import_native(source->is_leaf() ? source : source->rightmost());
	}
	insertion_images(const insertion_images&) = delete;
	insertion_images& operator=(const insertion_images&) = delete;

	image* root() const noexcept { return root_; }
	image*& __root() noexcept { return root_; }
	image* rightmost() const noexcept { return rightmost_; }
	image*& __rightmost() noexcept { return rightmost_; }
	image* find_native(NativeNode* source) const noexcept {
		const auto found = by_source_.find(source);
		return found == by_source_.end() ? nullptr : found->second;
	}
	image* import_native(NativeNode* source) {
		if (!source) return nullptr;
		const auto found = by_source_.find(source);
		if (found != by_source_.end()) return found->second;
		images_.emplace_back(*this, source);
		auto* result = &images_.back();
		by_source_.emplace(source, result);
		return result;
	}
	image* new_leaf_node(image* parent) { return create(allocation_kind::leaf, parent, NativeNode::kNodeValues); }
	image* new_internal_node(image* parent) { return create(allocation_kind::internal, parent, NativeNode::kNodeValues); }
	image* new_internal_root_node() { return create(allocation_kind::root, root_->get_parent(), NativeNode::kNodeValues); }
	image* new_leaf_root_node(int capacity) {
		auto* result = create(allocation_kind::leaf, nullptr, capacity);
		result->parent.staged = result;
		return result;
	}
	void delete_unpublished_root(image* node) noexcept { node->retired = true; }
	std::size_t image_count() const noexcept { return images_.size(); }
	auto& nodes() noexcept { return images_; }

private:
	using index_value = std::pair<NativeNode* const, image*>;
	Allocator allocator_;
	std::list<image, rebound<image>> images_;
	std::unordered_map<NativeNode*, image*, std::hash<NativeNode*>, std::equal_to<NativeNode*>, rebound<index_value>> by_source_;
	image* root_ = nullptr;
	image* rightmost_ = nullptr;

	image* create(allocation_kind kind, image* parent, int capacity) {
		images_.emplace_back(*this, kind, parent, capacity);
		return &images_.back();
	}
};

} // namespace btree::detail
