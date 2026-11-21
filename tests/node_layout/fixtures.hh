// Check counter and the counting allocator fixtures.
// Internal: included only by tests/node_layout.cc, inside its translation unit.

#pragma once

namespace {
unsigned checks = 0;
void require(bool condition) {
	++checks;
	if (!condition) throw std::runtime_error("compact node layout regression");
}
struct Allocations {
	struct Extent { std::size_t bytes, alignment; };
	std::map<void*, Extent> live;
	std::vector<std::size_t> requests;
	std::size_t bytes = 0;
	bool fail = false;
	int remaining = -1;
};
template <class T> struct CountingAllocator {
	using value_type = T;
	std::shared_ptr<Allocations> state;
	explicit CountingAllocator(std::shared_ptr<Allocations> value) : state(std::move(value)) {}
	template <class U> CountingAllocator(const CountingAllocator<U>& other) : state(other.state) {}
	T* allocate(std::size_t count) {
		if (state->fail || state->remaining == 0) throw std::bad_alloc();
		if (state->remaining > 0) --state->remaining;
		auto pointer = std::allocator<T>{}.allocate(count);
		try {
			require(state->live.emplace(pointer, Allocations::Extent{count * sizeof(T), alignof(T)}).second);
			state->requests.push_back(count * sizeof(T));
		} catch (...) {
			state->live.erase(pointer);
			std::allocator<T>{}.deallocate(pointer, count);
			throw;
		}
		state->bytes += count * sizeof(T);
		return pointer;
	}
	void deallocate(T* pointer, std::size_t count) {
		auto found = state->live.find(pointer);
		require(found != state->live.end());
		require(found->second.bytes == count * sizeof(T) && found->second.alignment == alignof(T));
		state->bytes -= found->second.bytes;
		state->live.erase(found);
		std::allocator<T>{}.deallocate(pointer, count);
	}
	template <class U> bool operator==(const CountingAllocator<U>& other) const { return state == other.state; }
	template <class U> bool operator!=(const CountingAllocator<U>& other) const { return !(*this == other); }
};
template <class Tree> void allocated_size(const Tree& tree, const Allocations& state) {
	require(tree.bytes_used() == sizeof(Tree) + state.bytes);
}
}
