#include "btree/map.h"
#include "btree/set.h"
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
void compact_roots() {
	using Params = btree::btree_set_params<int, std::less<int>, CountingAllocator<int>, 256>;
	using Node = btree::btree_node<Params>;
	using Set = btree::set<int, std::less<int>, CountingAllocator<int>>;
	auto state = std::make_shared<Allocations>();
	{
		Set values(std::less<int>{}, CountingAllocator<int>{state});
		allocated_size(values, *state);
		for (int i = 0; i < 3; ++i) {
			values.emplace(i);
			require(state->requests.back() == sizeof(typename Node::base_fields) + (std::size_t{1} << i) * sizeof(int));
			allocated_size(values, *state);
		}
		for (int i = 3; i < 4096; ++i) values.emplace(i);
		values.verify(); require(values.height() > 2);
		allocated_size(values, *state);
		while (!values.empty()) {
			values.erase(values.begin());
			values.verify(); allocated_size(values, *state);
		}
	}
	require(state->bytes == 0 && state->live.empty());
}
struct ThreeBytes {
	unsigned char high = 0, middle = 0, low = 0;
	bool operator<(const ThreeBytes& other) const {
		return high != other.high ? high < other.high : middle != other.middle ? middle < other.middle : low < other.low;
	}
};
void unpadded_leaf_extents() {
	static_assert(sizeof(ThreeBytes) == 3);
	using Set = btree::set<ThreeBytes, std::less<ThreeBytes>, CountingAllocator<ThreeBytes>, 32>;
	auto state = std::make_shared<Allocations>();
	{
		Set values(std::less<ThreeBytes>{}, CountingAllocator<ThreeBytes>{state});
		for (unsigned i = 0; i < 128; ++i) values.emplace(ThreeBytes{0, 0, static_cast<unsigned char>(i)});
		values.verify(); allocated_size(values, *state);
		while (!values.empty()) {
			values.erase(values.begin());
			allocated_size(values, *state);
		}
	}
	require(state->live.empty());
}
struct Payload {
	inline static int live = 0;
	int value;
	explicit Payload(int number) : value(number) { ++live; }
	Payload(const Payload&) = delete;
	Payload(Payload&& other) noexcept : value(other.value) { ++live; }
	Payload& operator=(Payload&&) = default;
	~Payload() { --live; }
};
void move_only_values() {
	using Value = std::pair<const int, Payload>;
	using Map = btree::map<int, Payload, std::less<int>, CountingAllocator<Value>>;
	auto state = std::make_shared<Allocations>();
	{
		Map values(std::less<int>{}, CountingAllocator<Value>{state});
		for (int i = 0; i < 2048; ++i) {
			values.emplace(i, Payload(i));
			require(Payload::live == static_cast<int>(values.size()));
		}
		values.verify(); allocated_size(values, *state);
		for (int i = 0; i < 2048; i += 2) values.erase(i);
		for (const auto& entry : values) require(entry.first == entry.second.value && entry.first % 2);
		values.verify(); allocated_size(values, *state);
		values.clear();
		require(Payload::live == 0); allocated_size(values, *state);
	}
	require(state->live.empty());
}
struct alignas(64) AlignedKey {
	int value = 0;
	AlignedKey() = default;
	explicit AlignedKey(int number) : value(number) {}
	bool operator<(const AlignedKey& other) const { return value < other.value; }
};
void aligned_slots() {
	using Set = btree::set<AlignedKey, std::less<AlignedKey>, CountingAllocator<AlignedKey>>;
	auto state = std::make_shared<Allocations>();
	{
		Set values(std::less<AlignedKey>{}, CountingAllocator<AlignedKey>{state});
		for (int i = 0; i < 512; ++i) values.emplace(i);
		values.verify(); allocated_size(values, *state);
		for (const auto& value : values) require(reinterpret_cast<std::uintptr_t>(&value) % alignof(AlignedKey) == 0);
		{
			auto copied = values;
			copied.verify(); require(copied.size() == values.size());
			for (const auto& value : copied) require(reinterpret_cast<std::uintptr_t>(&value) % alignof(AlignedKey) == 0);
		}
		for (int i = 0; i < 512; ++i) {
			values.erase(AlignedKey(i));
			values.verify(); allocated_size(values, *state);
		}
	}
	require(state->live.empty());
}
void allocation_failure() {
	auto state = std::make_shared<Allocations>();
	btree::set<int, std::less<int>, CountingAllocator<int>> values(std::less<int>{}, CountingAllocator<int>{state});
	values.emplace(1);
	const auto before = state->bytes;
	state->fail = true;
	bool rejected = false;
	try { values.emplace(2); } catch (const std::bad_alloc&) { rejected = true; }
	state->fail = false;
	require(rejected && values.size() == 1 && *values.begin() == 1 && state->bytes == before);
	values.verify(); allocated_size(values, *state);
}
struct ThrowingValue {
	inline static std::set<const ThrowingValue*> live;
	inline static int remaining = -1;
	int value;
	char padding[32]{};
	explicit ThrowingValue(int number) : value(number) { require(live.insert(this).second); }
	ThrowingValue(const ThrowingValue& other) : value(other.value) {
		if (remaining == 0) throw std::runtime_error("positioned value copy");
		if (remaining > 0) --remaining;
		require(live.insert(this).second);
	}
	ThrowingValue(ThrowingValue&& other) : ThrowingValue(static_cast<const ThrowingValue&>(other)) {}
	ThrowingValue& operator=(const ThrowingValue& other) { value = other.value; return *this; }
	ThrowingValue& operator=(ThrowingValue&& other) { value = other.value; return *this; }
	~ThrowingValue() { require(live.erase(this) == 1); }
};
using ThrowingMap = btree::map<int, ThrowingValue, std::less<int>,
	CountingAllocator<std::pair<const int, ThrowingValue>>>;
void check_contents(const ThrowingMap& values, int size) {
	require(values.size() == static_cast<std::size_t>(size));
	int key = 0;
	for (const auto& entry : values) {
		require(entry.first == key && entry.second.value == key);
		++key;
	}
	require(key == size);
	values.verify();
}
struct CopyOrderCompare {
	inline static bool reject = false;
	bool operator()(int left, int right) const { require(!reject); return left > right; }
};
void compact_copy_boundaries() {
	using Params = btree::btree_map_params<int, int, CopyOrderCompare,
		std::allocator<std::pair<const int, int>>, 256>;
	constexpr int capacity = btree::btree_node<Params>::kNodeValues;
	for (int size : {0, 1, capacity - 1, capacity, capacity + 1,
		(capacity + 1) * (capacity + 1) - 1, (capacity + 1) * (capacity + 1),
		(capacity + 1) * (capacity + 1) + 1}) {
		btree::map<int, int, CopyOrderCompare> source;
		for (int key = 0; key < size; ++key) source.emplace(key * 2, key * 2);
		CopyOrderCompare::reject = true;
		auto copied = source;
		CopyOrderCompare::reject = false;
		copied.verify();
		require(copied.size() == source.size() && std::equal(copied.begin(), copied.end(), source.begin()));
		copied.emplace(-1, -1); copied.emplace(size * 2 + 1, size * 2 + 1);
		if (size > 1) copied.emplace(size | 1, size | 1);
		copied.verify();
		while (!copied.empty()) { copied.erase(copied.begin()); copied.verify(); }
		source.verify(); require(source.size() == static_cast<std::size_t>(size));
	}
	btree::multimap<int, int, CopyOrderCompare> source;
	for (int value = 0; value < 1024; ++value) source.emplace(value % 17, value);
	CopyOrderCompare::reject = true;
	auto copied = source;
	CopyOrderCompare::reject = false;
	copied.verify();
	require(copied.size() == source.size() && std::equal(copied.begin(), copied.end(), source.begin()));
}
void copy_failure_positions() {
	for (int size : {0, 1, 3, 40, 300}) {
		auto state = std::make_shared<Allocations>();
		{
			ThrowingMap source(std::less<int>{}, CountingAllocator<std::pair<const int, ThrowingValue>>{state});
			for (int key = 0; key < size; ++key) source.emplace(key, key);
			const auto objects = ThrowingValue::live;
			const auto bytes = state->bytes;
			const auto nodes = state->live.size();
			for (int position = 0; position <= size; ++position) {
				ThrowingValue::remaining = position;
				bool rejected = false;
				try {
					ThrowingMap copied(source);
					check_contents(copied, size);
					require(copied.bytes_used() == sizeof(ThrowingMap) + state->bytes - bytes);
				} catch (const std::runtime_error& error) {
					if (std::string_view(error.what()) != "positioned value copy") throw;
					rejected = true;
				}
				ThrowingValue::remaining = -1;
				require(rejected == (position < size));
				require(ThrowingValue::live == objects && state->bytes == bytes && state->live.size() == nodes);
				check_contents(source, size);
			}
			const auto before_requests = state->requests.size();
			{ ThrowingMap copied(source); check_contents(copied, size); }
			const auto copy_requests = state->requests.size() - before_requests;
			for (std::size_t position = 0; position <= copy_requests; ++position) {
				state->remaining = static_cast<int>(position);
				bool rejected = false;
				try { ThrowingMap copied(source); check_contents(copied, size); }
				catch (const std::bad_alloc&) { rejected = true; }
				state->remaining = -1;
				require(rejected == (position < copy_requests));
				require(ThrowingValue::live == objects && state->bytes == bytes && state->live.size() == nodes);
				check_contents(source, size);
			}
		}
		require(ThrowingValue::live.empty() && state->live.empty() && state->bytes == 0);
	}
}
void compact_copy_history() {
	btree::map<int, int> pruned, direct;
	for (int key = 0; key < 4096; ++key) pruned.emplace(key, key);
	for (int key = 0; key < 4096; ++key) if (key % 4) pruned.erase(key);
	for (int key = 4092; key >= 0; key -= 4) direct.emplace(key, key);
	require(pruned.size() == direct.size() && std::equal(pruned.begin(), pruned.end(), direct.begin()));
	auto first = pruned;
	auto second = direct;
	first.verify(); second.verify();
	require(first.bytes_used() < pruned.bytes_used());
	require(first.bytes_used() == second.bytes_used());
}
void assignment_allocation_failure_positions() {
	for (int size : {1, 40, 300}) {
		auto source_state = std::make_shared<Allocations>();
		auto target_state = std::make_shared<Allocations>();
		{
			ThrowingMap source(std::less<int>{}, CountingAllocator<std::pair<const int, ThrowingValue>>{source_state});
			for (int key = 0; key < size; ++key) source.try_emplace(key, key);
			const auto objects = ThrowingValue::live;
			const auto bytes = source_state->bytes;
			const auto allocations = source_state->live.size();
			const auto before_requests = source_state->requests.size();
			{ ThrowingMap copied(source); check_contents(copied, size); }
			const auto requests = source_state->requests.size() - before_requests;
			for (std::size_t budget = 0; budget <= requests; ++budget) {
				{
					ThrowingMap target(std::less<int>{}, CountingAllocator<std::pair<const int, ThrowingValue>>{target_state});
					target.try_emplace(-1, -1);
					source_state->remaining = static_cast<int>(budget);
					bool rejected = false;
					try { target = source; } catch (const std::bad_alloc&) { rejected = true; }
					source_state->remaining = -1;
					require(rejected == (budget < requests));
					check_contents(target, rejected ? 0 : size);
					require(target_state->live.empty() && target_state->bytes == 0);
					if (rejected) require(ThrowingValue::live == objects && source_state->bytes == bytes && source_state->live.size() == allocations);
				}
				require(ThrowingValue::live == objects && source_state->bytes == bytes && source_state->live.size() == allocations);
				check_contents(source, size);
			}
		}
		require(ThrowingValue::live.empty() && source_state->live.empty() && target_state->live.empty());
	}
}
void assignment_failure_positions() {
	for (int size : {1, 40, 300}) {
		auto source_state = std::make_shared<Allocations>();
		auto target_state = std::make_shared<Allocations>();
		{
			ThrowingMap source(std::less<int>{}, CountingAllocator<std::pair<const int, ThrowingValue>>{source_state});
			for (int key = 0; key < size; ++key) source.emplace(key, key);
			const auto objects = ThrowingValue::live;
			const auto bytes = source_state->bytes;
			for (int position = 0; position <= size; ++position) {
				{
					ThrowingMap target(std::less<int>{}, CountingAllocator<std::pair<const int, ThrowingValue>>{target_state});
					target.emplace(-1, -1);
					ThrowingValue::remaining = position;
					bool rejected = false;
					try { target = source; }
					catch (const std::runtime_error& error) {
						if (std::string_view(error.what()) != "positioned value copy") throw;
						rejected = true;
					}
					ThrowingValue::remaining = -1;
					require(rejected == (position < size));
					check_contents(target, rejected ? 0 : size);
					require(target_state->live.empty() && target_state->bytes == 0);
					if (rejected) require(ThrowingValue::live == objects && source_state->bytes == bytes);
				}
				require(ThrowingValue::live == objects && source_state->bytes == bytes);
				check_contents(source, size);
			}
		}
		require(ThrowingValue::live.empty() && source_state->live.empty() && target_state->live.empty());
	}
}
void nested_copy_failure_positions() {
	using Entry = std::pair<const int, ThrowingMap>;
	using Nested = btree::map<int, ThrowingMap, std::less<int>, CountingAllocator<Entry>>;
	auto state = std::make_shared<Allocations>();
	{
		Nested source(std::less<int>{}, CountingAllocator<Entry>{state});
		for (int group = 0; group < 4; ++group) {
			ThrowingMap inner(std::less<int>{}, CountingAllocator<std::pair<const int, ThrowingValue>>{state});
			for (int key = 0; key < 40; ++key) inner.emplace(key, key);
			source.emplace(group, inner);
		}
		const auto objects = ThrowingValue::live;
		const auto bytes = state->bytes;
		const auto nodes = state->live.size();
		for (int position = 0; position <= 160; ++position) {
			ThrowingValue::remaining = position;
			bool rejected = false;
			try {
				Nested copied(source);
				require(copied.size() == 4);
				copied.verify();
				for (const auto& entry : copied) check_contents(entry.second, 40);
			} catch (const std::runtime_error& error) {
				if (std::string_view(error.what()) != "positioned value copy") throw;
				rejected = true;
			}
			ThrowingValue::remaining = -1;
			require(rejected == (position < 160));
			require(ThrowingValue::live == objects && state->bytes == bytes && state->live.size() == nodes);
			source.verify();
			for (const auto& entry : source) check_contents(entry.second, 40);
		}
	}
	require(ThrowingValue::live.empty() && state->live.empty() && state->bytes == 0);
}
void tail_copy_failure() {
	auto state = std::make_shared<Allocations>();
	{
		ThrowingMap values(std::less<int>{}, CountingAllocator<std::pair<const int, ThrowingValue>>{state});
		for (int key = 0; key < 41; ++key) values.try_emplace(key, key);
		int size = 41;
		for (;;) {
			const auto allocations = state->requests.size();
			values.try_emplace(size, size);
			++size;
			if (state->requests.size() != allocations) break;
		}
		ThrowingValue incoming(size);
		const auto objects = ThrowingValue::live;
		const auto bytes = state->bytes;
		ThrowingValue::remaining = 0;
		bool rejected = false;
		try { values.try_emplace(size, incoming); }
		catch (const std::runtime_error& error) {
			if (std::string_view(error.what()) != "positioned value copy") throw;
			rejected = true;
		}
		ThrowingValue::remaining = -1;
		require(rejected && state->bytes == bytes && ThrowingValue::live == objects);
		check_contents(values, size);
		values.try_emplace(size, incoming);
		check_contents(values, size + 1);
	}
	require(ThrowingValue::live.empty() && state->live.empty() && state->bytes == 0);
}
template <class Tree> void move_family() {
	static_assert(std::is_nothrow_move_constructible<Tree>::value);
	static_assert(std::is_nothrow_move_assignable<Tree>::value);
	static_assert(std::is_copy_constructible<Tree>::value && std::is_copy_assignable<Tree>::value);
	Tree source;
	for (int key = 0; key < 64; ++key) {
		if constexpr (std::is_same<typename Tree::value_type, typename Tree::key_type>::value) source.emplace(key);
		else source.emplace(key, key);
	}
	Tree moved(std::move(source));
	require(source.empty() && moved.size() == 64);
	Tree assigned;
	assigned = std::move(moved);
	require(moved.empty() && assigned.size() == 64);
	auto* same = &assigned;
	assigned = std::move(*same);
	require(assigned.size() == 64);
	Tree copied(assigned);
	copied = assigned;
	require(copied.size() == 64);
	if constexpr (std::is_same<typename Tree::value_type, typename Tree::key_type>::value) source.emplace(72);
	else source.emplace(72, 72);
	require(source.size() == 1 && source.count(72) == 1);
	if constexpr (std::is_same<typename Tree::value_type, typename Tree::key_type>::value) {
		auto inserted = source.insert(source.end(), 73L);
		require(*inserted == 73);
	} else {
		auto inserted = source.insert(source.end(), std::make_pair(73, 73));
		require(inserted->first == 73 && inserted->second == 73);
	}
	require(source.size() == 2);
	source.verify(); assigned.verify(); copied.verify();
}
struct NonDefaultCompare {
	NonDefaultCompare() = delete;
	explicit NonDefaultCompare(int) {}
	bool operator()(int left, int right) const { return left < right; }
};
void explicit_comparator_construction() {
	using Tree = btree::map<int, int, NonDefaultCompare>;
	static_assert(!std::is_default_constructible<Tree>::value);
	Tree values(NonDefaultCompare(1));
	values.emplace(1, 1);
	require(values.at(1) == 1);
	values.verify();
}
void move_ownership() {
	static_assert(!std::is_default_constructible<ThrowingMap>::value);
	static_assert(std::is_constructible<ThrowingMap, std::less<int>,
		CountingAllocator<std::pair<const int, ThrowingValue>>>::value);
	move_family<btree::map<int, int>>();
	move_family<btree::multimap<int, int>>();
	move_family<btree::set<int>>();
	move_family<btree::multiset<int>>();
	auto source_state = std::make_shared<Allocations>();
	auto target_state = std::make_shared<Allocations>();
	{
		ThrowingMap source(std::less<int>{}, CountingAllocator<std::pair<const int, ThrowingValue>>{source_state});
		for (int key = 0; key < 64; ++key) source.try_emplace(key, key);
		const auto objects = ThrowingValue::live;
		const auto requests = source_state->requests.size();
		const auto bytes = source_state->bytes;
		ThrowingValue::remaining = 0;
		source_state->fail = true;
		ThrowingMap moved(std::move(source));
		require(source.empty() && ThrowingValue::live == objects);
		check_contents(moved, 64);
		source_state->fail = false;
		ThrowingMap target(std::less<int>{}, CountingAllocator<std::pair<const int, ThrowingValue>>{target_state});
		target.try_emplace(-1, -1);
		source_state->fail = target_state->fail = true;
		target = std::move(moved);
		require(moved.empty() && target_state->live.empty() && target_state->bytes == 0);
		require(source_state->bytes == bytes && source_state->requests.size() == requests);
		require(ThrowingValue::live == objects);
		check_contents(target, 64);
		auto* same = &target;
		target = std::move(*same);
		check_contents(target, 64);
		source_state->fail = target_state->fail = false;
		ThrowingValue::remaining = -1;
		source.try_emplace(0, 0);
		moved.try_emplace(0, 0);
		check_contents(source, 1); check_contents(moved, 1);
	}
	require(ThrowingValue::live.empty() && source_state->live.empty() && target_state->live.empty());
}
struct ThrowingCompare {
	inline static bool reject_copy = false, reject_assignment = false;
	ThrowingCompare() = default;
	ThrowingCompare(const ThrowingCompare&) {
		if (reject_copy) throw std::runtime_error("comparator copy");
	}
	ThrowingCompare& operator=(const ThrowingCompare&) {
		if (reject_assignment) throw std::runtime_error("comparator assignment");
		return *this;
	}
	bool operator()(int left, int right) const { return left < right; }
};
template <typename Tree>
void first_insert_without_search() {
	Tree values;
	ThrowingCompare::reject_copy = true;
	if constexpr (std::is_same<typename Tree::key_type, typename Tree::value_type>::value) values.emplace(1);
	else values.emplace(1, 1);
	ThrowingCompare::reject_copy = false;
	values.verify(); require(values.size() == 1);
}
void first_insert_preparation() {
	first_insert_without_search<btree::map<int, int, ThrowingCompare>>();
	first_insert_without_search<btree::multimap<int, int, ThrowingCompare>>();
	first_insert_without_search<btree::set<int, ThrowingCompare>>();
	first_insert_without_search<btree::multiset<int, ThrowingCompare>>();
	{
		btree::map<int, ThrowingValue, ThrowingCompare> values;
		ThrowingValue incoming(1);
		ThrowingCompare::reject_copy = true;
		ThrowingValue::remaining = 0;
		bool rejected = false;
		try { values.try_emplace(1, incoming); }
		catch (const std::runtime_error& error) {
			if (std::string_view(error.what()) != "positioned value copy") throw;
			rejected = true;
		}
		ThrowingValue::remaining = -1;
		require(rejected && values.empty());
		values.try_emplace(1, incoming);
		ThrowingCompare::reject_copy = false;
		values.verify(); require(values.size() == 1);
	}
	require(ThrowingValue::live.empty());
}
void move_preparation_failure() {
	using Tree = btree::map<int, int, ThrowingCompare>;
	static_assert(!std::is_nothrow_move_constructible<Tree>::value);
	static_assert(!std::is_nothrow_move_assignable<Tree>::value);
	Tree source;
	source.emplace(1, 1);
	ThrowingCompare::reject_copy = true;
	bool rejected = false;
	try { Tree moved(std::move(source)); }
	catch (const std::runtime_error&) { rejected = true; }
	ThrowingCompare::reject_copy = false;
	require(rejected && source.size() == 1 && source.at(1) == 1);
	Tree target;
	target.emplace(2, 2);
	ThrowingCompare::reject_assignment = true;
	rejected = false;
	try { target = std::move(source); }
	catch (const std::runtime_error&) { rejected = true; }
	ThrowingCompare::reject_assignment = false;
	require(rejected && target.empty() && source.size() == 1 && source.at(1) == 1);
	target.emplace(3, 3);
	source.verify(); target.verify();
}
void split_allocation_failure() {
	using Params = btree::btree_map_params<int, ThrowingValue, std::less<int>,
		CountingAllocator<std::pair<const int, ThrowingValue>>, 256>;
	constexpr int capacity = btree::btree_node<Params>::kNodeValues;
	auto state = std::make_shared<Allocations>();
	{
		ThrowingMap values(std::less<int>{}, CountingAllocator<std::pair<const int, ThrowingValue>>{state});
		for (int key = 0; key < capacity; ++key) values.try_emplace(key, key);
		state->remaining = 1;
		bool rejected = false;
		try { values.try_emplace(capacity, capacity); }
		catch (const std::bad_alloc&) { rejected = true; }
		state->remaining = -1;
		std::cout << "split allocation: rejected=" << rejected << " height=" << values.height() << '\n';
		require(rejected && values.height() == 1);
		check_contents(values, capacity);
		allocated_size(values, *state);
	}
	require(ThrowingValue::live.empty() && state->live.empty() && state->bytes == 0);
}
void split_separator_failure() {
	using Params = btree::btree_map_params<int, ThrowingValue, std::less<int>,
		CountingAllocator<std::pair<const int, ThrowingValue>>, 256>;
	constexpr int capacity = btree::btree_node<Params>::kNodeValues;
	auto state = std::make_shared<Allocations>();
	{
		ThrowingMap values(std::less<int>{}, CountingAllocator<std::pair<const int, ThrowingValue>>{state});
		for (int key = 0; key < capacity; ++key) values.try_emplace(key, key);
		ThrowingValue::remaining = 0;
		bool rejected = false;
		try { values.try_emplace(capacity, capacity); }
		catch (const std::runtime_error& error) {
			if (std::string_view(error.what()) != "positioned value copy") throw;
			rejected = true;
		}
		ThrowingValue::remaining = -1;
		std::size_t visited = 0;
		for (const auto& entry : values) {
			require(entry.first == static_cast<int>(visited));
			++visited;
		}
		const auto accounted = values.bytes_used() - sizeof(ThrowingMap);
		std::cout << "split separator: rejected=" << rejected << " expected=" << capacity
			<< " visited=" << visited << " allocated=" << state->bytes << " accounted=" << accounted << '\n';
		require(rejected && visited == static_cast<std::size_t>(capacity) && accounted == state->bytes);
		check_contents(values, capacity);
	}
	require(ThrowingValue::live.empty() && state->live.empty() && state->bytes == 0);
}
void compact_root_copy_failure() {
	for (int size : {0, 1, 2, 4}) {
		for (int position = 0; position < size + 1; ++position) {
			auto state = std::make_shared<Allocations>();
			{
				ThrowingMap values(std::less<int>{}, CountingAllocator<std::pair<const int, ThrowingValue>>{state});
				for (int key = 0; key < size; ++key) values.emplace(key, key);
				const auto objects = ThrowingValue::live;
				ThrowingValue incoming(size);
				ThrowingValue::remaining = position;
				bool rejected = false;
				try { values.emplace(size, incoming); }
				catch (const std::runtime_error& error) {
					if (std::string_view(error.what()) != "positioned value copy") throw;
					rejected = true;
				}
				ThrowingValue::remaining = -1;
				require(rejected);
				check_contents(values, size);
				require(ThrowingValue::live.size() == objects.size() + 1);
				allocated_size(values, *state);
				values.emplace(size, incoming);
				check_contents(values, size + 1);
			}
			require(ThrowingValue::live.empty() && state->live.empty() && state->bytes == 0);
		}
	}
}
template <class T> struct MisalignedAllocator {
	using value_type = T;
	std::shared_ptr<unsigned> live;
	explicit MisalignedAllocator(std::shared_ptr<unsigned> value) : live(std::move(value)) {}
	template <class U> MisalignedAllocator(const MisalignedAllocator<U>& other) : live(other.live) {}
	T* allocate(std::size_t count) {
		auto storage = static_cast<char*>(::operator new(count * sizeof(T) + 1));
		++*live;
		return reinterpret_cast<T*>(storage + 1);
	}
	void deallocate(T* pointer, std::size_t) {
		--*live;
		::operator delete(reinterpret_cast<char*>(pointer) - 1);
	}
	template <class U> bool operator==(const MisalignedAllocator<U>& other) const { return live == other.live; }
	template <class U> bool operator!=(const MisalignedAllocator<U>& other) const { return !(*this == other); }
};
void rejected_alignment() {
	auto live = std::make_shared<unsigned>(0);
	btree::set<int, std::less<int>, MisalignedAllocator<int>> values(std::less<int>{}, MisalignedAllocator<int>{live});
	bool rejected = false;
	try { values.emplace(1); } catch (const std::bad_alloc&) { rejected = true; }
	require(rejected && values.empty() && *live == 0);
}
void short_root_lifetime() {
	btree::map<std::string, std::string> values;
	values.emplace("first", "one");
	require(values.at("first") == "one");
	values.emplace("second", "two");
	values.verify();
	require(values.size() == 2);
}
template <typename Tree, typename Reference>
void range_insert_family() {
	constexpr bool is_set = std::is_same<typename Tree::key_type, typename Tree::value_type>::value;
	for (bool populated : {false, true}) for (bool reversed : {false, true}) {
		Tree values;
		Reference expected;
		if (populated) {
			if constexpr (is_set) { values.insert(-1); expected.insert(-1); }
			else { values.emplace(-1, -1); expected.emplace(-1, -1); }
		}
		std::vector<typename Tree::value_type> input;
		for (int i = 0; i < 128; ++i) {
			const int key = ((reversed ? 127 - i : i) * 37) % 61;
			if constexpr (is_set) input.emplace_back(key);
			else input.emplace_back(key, i);
		}
		values.insert(input.cbegin(), input.cend());
		expected.insert(input.cbegin(), input.cend());
		values.verify();
		require(values.size() == expected.size() && std::equal(values.begin(), values.end(), expected.begin()));
		Tree constructed(input.cbegin(), input.cend());
		Reference constructed_expected(input.cbegin(), input.cend());
		constructed.verify();
		require(constructed.size() == constructed_expected.size() &&
			std::equal(constructed.begin(), constructed.end(), constructed_expected.begin()));
	}
}
void range_insertions() {
	range_insert_family<btree::map<int, int>, std::map<int, int>>();
	range_insert_family<btree::multimap<int, int>, std::multimap<int, int>>();
	range_insert_family<btree::set<int>, std::set<int>>();
	range_insert_family<btree::multiset<int>, std::multiset<int>>();
	btree::map<int, int> unique_map; unique_map.insert({{2, 2}, {1, 1}, {3, 3}});
	btree::multimap<int, int> multi_map; multi_map.insert({{2, 2}, {1, 1}, {2, 3}});
	btree::set<int> unique_set; unique_set.insert({2, 1, 3});
	btree::multiset<int> multi_set; multi_set.insert({2, 1, 2});
	unique_map.verify(); multi_map.verify(); unique_set.verify(); multi_set.verify();
	require(unique_map.size() == 3 && multi_map.size() == 3 && unique_set.size() == 3 && multi_set.size() == 3);
}
void ordered_model() {
	btree::set<int> values;
	std::set<int> expected;
	std::mt19937 random(21917);
	for (unsigned turn = 0; turn < 12000; ++turn) {
		auto key = static_cast<int>(random() % 2048);
		if (random() & 1) { values.emplace(key); expected.emplace(key); }
		else { values.erase(key); expected.erase(key); }
		values.verify();
		require(values.size() == expected.size() && std::equal(values.begin(), values.end(), expected.begin()));
	}
}
}
int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--split-allocation-only") {
			split_allocation_failure();
		} else if (argc == 2 && std::string_view(argv[1]) == "--tail-copy-only") {
			tail_copy_failure();
		} else if (argc == 2 && std::string_view(argv[1]) == "--split-separator-only") {
			split_separator_failure();
		} else if (argc == 2 && std::string_view(argv[1]) == "--short-root-only") {
			short_root_lifetime();
		} else {
			require(argc == 1);
			short_root_lifetime(); compact_roots(); unpadded_leaf_extents(); move_only_values();
			range_insertions(); aligned_slots(); allocation_failure(); rejected_alignment(); ordered_model();
			first_insert_preparation(); move_ownership(); move_preparation_failure(); explicit_comparator_construction();
			compact_copy_history(); assignment_allocation_failure_positions();
			compact_copy_boundaries(); copy_failure_positions(); assignment_failure_positions(); nested_copy_failure_positions(); compact_root_copy_failure(); tail_copy_failure(); split_allocation_failure();
		}
	}
	catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
	std::cout << checks << " compact layout checks passed\n";
}
