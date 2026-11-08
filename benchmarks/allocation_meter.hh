#pragma once

#include <btree/map.h>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace btree_bench {
struct AllocationMeter {
	std::size_t live = 0, peak = 0, calls = 0, requested = 0;
};
template <typename T>
struct MeterAllocator {
	using value_type = T;
	std::shared_ptr<AllocationMeter> meter;
	explicit MeterAllocator(std::shared_ptr<AllocationMeter> value) : meter(std::move(value)) {}
	template <typename U> MeterAllocator(const MeterAllocator<U>& other) : meter(other.meter) {}
	T* allocate(std::size_t count) {
		auto result = std::allocator<T>{}.allocate(count);
		const auto bytes = count * sizeof(T);
		meter->live += bytes; meter->requested += bytes; ++meter->calls;
		meter->peak = std::max(meter->peak, meter->live);
		return result;
	}
	void deallocate(T* pointer, std::size_t count) noexcept {
		meter->live -= count * sizeof(T);
		std::allocator<T>{}.deallocate(pointer, count);
	}
	template <typename U> bool operator==(const MeterAllocator<U>& other) const noexcept { return meter == other.meter; }
	template <typename U> bool operator!=(const MeterAllocator<U>& other) const noexcept { return !(*this == other); }
};
template <typename Value>
using Map = btree::map<std::string, Value, std::less<std::string>, MeterAllocator<std::pair<const std::string, Value>>>;

} // namespace btree_bench
