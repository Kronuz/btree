// Copyright (c) 2013 Google Inc. and (c) 2019 Germán Méndez Bravo.
// Licensed under the Apache License, Version 2.0. See LICENSE.
#pragma once

#include <tuple>

namespace btree {

template <typename P>
class btree<P>::insertion_transaction {
	static constexpr bool is_map = !std::is_same<key_type, value_type>::value;
	static constexpr bool fast = std::is_nothrow_move_constructible<key_type>::value &&
		(!is_map || (std::is_copy_constructible<key_type>::value && std::is_nothrow_move_constructible<mapped_type>::value));
	using incoming_type = std::conditional_t<is_map, std::pair<key_type, mapped_type>, key_type>;
	using images_type = detail::insertion_images<node_type, internal_allocator_type>;
	using image_type = typename images_type::image;
	using edge_type = typename images_type::edge;
	using slot_id = typename images_type::slot_id;
	template <typename T>
	using rebound = typename internal_allocator_traits::template rebind_alloc<T>;
	struct transfer {
		value_type* original;
		std::optional<key_type> key;
		std::optional<mapped_type> mapped;
		explicit transfer(value_type* source) : original(source) {}
	};
	struct destination {
		image_type* image;
		node_type* node;
		node_type* parent = nullptr;
		std::vector<node_type*, rebound<node_type*>> children;
		std::vector<transfer*, rebound<transfer*>> transfers;
		bool owns = false;
		destination(image_type* source, const internal_allocator_type& allocator)
			: image(source), node(source->original), children(rebound<node_type*>(allocator)),
			  transfers(rebound<transfer*>(allocator)) {}
	};

	self_type& tree_;
	incoming_type incoming_;
	images_type images_;
	std::vector<destination, rebound<destination>> destinations_;
	std::list<transfer, rebound<transfer>> transfers_;
	bool consumed_set_key_ = false;
	bool committed_ = false;

	std::size_t extent(const image_type& image) const noexcept {
		using kind = typename images_type::allocation_kind;
		if (image.kind == kind::leaf) return node_type::leaf_size(image.capacity);
		return image.kind == kind::root ? node_type::root_size() : node_type::internal_size();
	}
	destination& find(image_type* image) noexcept {
		for (auto& result : destinations_) if (result.image == image) return result;
		std::terminate();
	}
	node_type* resolve(edge_type edge) noexcept {
		if (edge.staged) return edge.staged->destination;
		if (auto image = images_.find_native(edge.original)) return image->destination;
		return edge.original;
	}
	void prepare_destinations() {
		destinations_.reserve(images_.image_count());
		for (auto& image : images_.nodes()) destinations_.emplace_back(&image, tree_.internal_allocator());
		for (auto& destination : destinations_) {
			auto& image = *destination.image;
			if (image.retired) continue;
			if (!image.original || (!fast && (image.values_changed || image.count() != image.original_count))) {
				auto storage = tree_.allocate_node(extent(image));
				using kind = typename images_type::allocation_kind;
				if (image.kind == kind::leaf)
					destination.node = node_type::init_leaf(storage, nullptr, image.capacity);
				else if (image.kind == kind::root)
					destination.node = node_type::init_root(storage, nullptr, 0);
				else destination.node = node_type::init_internal(storage, nullptr);
				destination.owns = true;
				image.destination = destination.node;
			}
		}
		for (auto& destination : destinations_) {
			auto& image = *destination.image;
			if (destination.owns && image.original && &image != images_.root()) {
				auto parent = image.parent.staged ? image.parent.staged : images_.find_native(image.parent.original);
				assert(parent);
				parent->children_changed = true;
			}
		}
		for (auto& destination : destinations_) {
			auto& image = *destination.image;
			if (image.retired) continue;
			destination.parent = resolve(image.parent);
			if (destination.owns || image.values_changed) image.value_slots();
			if (!image.is_leaf() && (destination.owns || image.children_changed)) {
				destination.children.reserve(image.count() + 1);
				auto& edges = image.child_slots();
				for (int i = 0; i <= image.count(); ++i) destination.children.push_back(resolve(edges[i]));
			}
		}
	}
	void prepare_transfers() {
		for (auto& destination : destinations_) {
			auto& image = *destination.image;
			if (image.retired || (!destination.owns && !image.values_changed)) continue;
			destination.transfers.resize(image.count(), nullptr);
			for (int i = 0; i < image.count(); ++i) {
				auto id = image.value(i);
				if (id.source == destination.node && id.position == i) continue;
				transfers_.emplace_back(id.source ? &id.source->value(id.position) : nullptr);
				auto& entry = transfers_.back();
				if constexpr (is_map) {
					if (entry.original) entry.key.emplace(entry.original->first);
					else entry.key.emplace(std::move(incoming_.first));
				}
				destination.transfers[i] = &entry;
			}
		}
	}
	void materialize_fast() noexcept {
		for (auto& entry : transfers_) {
			if constexpr (is_map)
				entry.mapped.emplace(std::move(entry.original ? entry.original->second : incoming_.second));
			else entry.key.emplace(std::move(entry.original ? *entry.original : incoming_));
		}
		for (auto& entry : transfers_) if (entry.original) std::destroy_at(entry.original);
		for (auto& destination : destinations_) {
			for (int i = 0; i < static_cast<int>(destination.transfers.size()); ++i) {
				auto entry = destination.transfers[i];
				if (!entry) continue;
				if constexpr (is_map)
					destination.node->construct_value(i, std::piecewise_construct,
						std::forward_as_tuple(std::move(*entry->key)), std::forward_as_tuple(std::move(*entry->mapped)));
				else destination.node->construct_value(i, std::move(*entry->key));
			}
			destination.node->set_count(destination.image->retired ? 0 : destination.image->count());
		}
	}
	void construct_slot(node_type* destination, int position, slot_id id) {
		if constexpr (is_map) {
			if (id.source) {
				auto& original = id.source->value(id.position);
				if constexpr (std::is_copy_constructible<mapped_type>::value)
					destination->construct_value(position, std::piecewise_construct,
						std::forward_as_tuple(original.first), std::forward_as_tuple(original.second));
				else destination->construct_value(position, std::piecewise_construct,
					std::forward_as_tuple(original.first), std::forward_as_tuple(std::move(original.second)));
			} else {
				destination->construct_value(position, std::piecewise_construct,
					std::forward_as_tuple(std::move_if_noexcept(incoming_.first)),
					std::forward_as_tuple(std::move_if_noexcept(incoming_.second)));
			}
		} else {
			if (id.source) {
				if constexpr (std::is_copy_constructible<key_type>::value)
					destination->construct_value(position, id.source->value(id.position));
				else {
					consumed_set_key_ = true;
					destination->construct_value(position, std::move(id.source->value(id.position)));
				}
			} else destination->construct_value(position, std::move_if_noexcept(incoming_));
		}
	}
	void cleanup() noexcept {
		for (auto& destination : destinations_) if (destination.owns)
			tree_.deallocate_node(destination.node, extent(*destination.image));
		if (consumed_set_key_ && !committed_) tree_.clear();
	}

public:
	template <typename... Args>
	insertion_transaction(self_type& tree, Args&&... args)
		: tree_(tree), incoming_(std::forward<Args>(args)...), images_(tree.root(), tree.internal_allocator()),
		  destinations_(rebound<destination>(tree.internal_allocator())), transfers_(rebound<transfer>(tree.internal_allocator())) {}
	~insertion_transaction() { cleanup(); }
	insertion_transaction(const insertion_transaction&) = delete;
	insertion_transaction& operator=(const insertion_transaction&) = delete;

	iterator insert(iterator original) {
		typename images_type::cursor insertion(images_.import_native(original.node), original.position);
		if (insertion.node->count() == insertion.node->max_count()) {
			if (insertion.node->max_count() < kNodeValues) {
				auto old = insertion.node;
				auto replacement = images_.new_leaf_root_node(std::min<int>(kNodeValues, old->max_count() * 2));
				replacement->swap(old);
				old->retired = true;
				images_.__root() = images_.__rightmost() = insertion.node = replacement;
			} else detail::rebalance_or_split(images_, insertion);
		}
		insertion.node->insert_value(insertion.position, slot_id{nullptr, 0});
		for (auto ancestor = insertion.node; ancestor != images_.root(); ) ancestor = ancestor->get_parent();
		const auto final_size = tree_.size() + 1;
		prepare_destinations();
		auto new_root = find(images_.root()).node;
		auto new_rightmost = find(images_.rightmost()).node;
		auto result = iterator(find(insertion.node).node, insertion.position);
		if constexpr (fast) {
			prepare_transfers();
			materialize_fast();
		} else {
			for (auto& destination : destinations_) if (destination.owns) {
				auto& image = *destination.image;
				for (int i = 0; i < image.count(); ++i) {
					construct_slot(destination.node, i, image.value(i));
					destination.node->set_count(i + 1);
				}
			}
		}
		for (auto& destination : destinations_) {
			auto& image = *destination.image;
			if (image.retired) continue;
			destination.node->fields_.parent = destination.parent;
			destination.node->fields_.position = image.position();
			for (int i = 0; i < static_cast<int>(destination.children.size()); ++i)
				destination.node->set_child(i, destination.children[i]);
		}
		if (!new_root->is_leaf()) {
			new_root->__rightmost() = new_rightmost;
			new_root->__size() = final_size;
		}
		tree_.__root() = new_root;
		committed_ = true;
		for (auto& destination : destinations_) {
			if ((destination.owns || destination.image->retired) && destination.image->original)
				tree_.deallocate_node(destination.image->original, extent(*destination.image));
			destination.owns = false;
		}
		return result;
	}
};

} // namespace btree
