// Copyright (c) 2013 Google Inc. and (c) 2019 Germán Méndez Bravo.
// Licensed under the Apache License, Version 2.0. See LICENSE.
#pragma once

#include <limits>

namespace btree {

template <typename P>
class btree<P>::drain_cursor {
	friend class btree<P>;
	using state_type = detail::drain_state<node_type>;
	empty_base_handle<internal_allocator_type, state_type> state_;

	internal_allocator_type& allocator() noexcept { return static_cast<internal_allocator_type&>(state_); }
	const internal_allocator_type& allocator() const noexcept { return static_cast<const internal_allocator_type&>(state_); }
	explicit drain_cursor(self_type& source) noexcept(std::is_nothrow_copy_constructible<internal_allocator_type>::value)
		: state_(source.internal_allocator(), state_type(source.root(), source.root() && !source.root()->is_leaf())) {
		source.__root() = nullptr;
	}
	void finish() noexcept {
		while (!done()) step(std::numeric_limits<std::size_t>::max());
	}

public:
	template <typename A = internal_allocator_type, std::enable_if_t<std::is_default_constructible<A>::value, int> = 0>
	drain_cursor() noexcept(std::is_nothrow_default_constructible<A>::value && std::is_nothrow_copy_constructible<A>::value)
		: state_(A{}, state_type{}) {}
	~drain_cursor() noexcept { finish(); }
	drain_cursor(const drain_cursor&) = delete;
	drain_cursor& operator=(const drain_cursor&) = delete;
	drain_cursor(drain_cursor&& source) noexcept(std::is_nothrow_copy_constructible<internal_allocator_type>::value)
		: state_(source.allocator(), source.state_.data) {
		source.state_.data.reset();
	}
	drain_cursor& operator=(drain_cursor&& source) noexcept(std::is_nothrow_copy_assignable<internal_allocator_type>::value) {
		if (this != &source) {
			finish();
			allocator() = source.allocator();
			state_.data = source.state_.data;
			source.state_.data.reset();
		}
		return *this;
	}
	bool done() const noexcept { return state_.data.done(); }
	std::size_t step(std::size_t limit) noexcept { return state_.data.step(allocator(), limit); }
};

template <typename P>
inline typename btree<P>::drain_type btree<P>::drain() noexcept(std::is_nothrow_copy_constructible<internal_allocator_type>::value) {
	return drain_cursor(*this);
}

} // namespace btree
