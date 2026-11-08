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
					require(copied.bytes_used() == sizeof(ThrowingMap) + bytes);
				} catch (const std::runtime_error& error) {
					if (std::string_view(error.what()) != "positioned value copy") throw;
					rejected = true;
				}
				ThrowingValue::remaining = -1;
				require(rejected == (position < size));
				require(ThrowingValue::live == objects && state->bytes == bytes && state->live.size() == nodes);
				check_contents(source, size);
			}
			for (std::size_t position = 0; position <= nodes; ++position) {
				state->remaining = static_cast<int>(position);
				bool rejected = false;
				try { ThrowingMap copied(source); check_contents(copied, size); }
				catch (const std::bad_alloc&) { rejected = true; }
				state->remaining = -1;
				require(rejected == (position < nodes));
				require(ThrowingValue::live == objects && state->bytes == bytes && state->live.size() == nodes);
				check_contents(source, size);
			}
		}
		require(ThrowingValue::live.empty() && state->live.empty() && state->bytes == 0);
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
			aligned_slots(); allocation_failure(); rejected_alignment(); ordered_model();
			copy_failure_positions(); assignment_failure_positions(); nested_copy_failure_positions(); compact_root_copy_failure(); tail_copy_failure(); split_allocation_failure();
		}
	}
	catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
	std::cout << checks << " compact layout checks passed\n";
}
