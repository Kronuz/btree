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
};
template <class T> struct CountingAllocator {
	using value_type = T;
	std::shared_ptr<Allocations> state;
	explicit CountingAllocator(std::shared_ptr<Allocations> value) : state(std::move(value)) {}
	template <class U> CountingAllocator(const CountingAllocator<U>& other) : state(other.state) {}
	T* allocate(std::size_t count) {
		if (state->fail) throw std::bad_alloc();
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
			else { values.insert(typename Tree::value_type{-1, -1}); expected.emplace(-1, -1); }
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
struct FirstValue {
	inline static bool refuse_copy = false;
	inline static int live = 0;
	int value;
	explicit FirstValue(int input) : value(input) { ++live; }
	FirstValue(const FirstValue& other) : value(other.value) {
		if (refuse_copy) throw std::runtime_error("first value copy");
		++live;
	}
	FirstValue(FirstValue&& other) noexcept : value(other.value) { ++live; }
	FirstValue& operator=(FirstValue&&) = default;
	~FirstValue() { --live; }
	bool operator<(const FirstValue& other) const { return value < other.value; }
};
struct RefusingCompare {
	inline static bool refuse_copy = false;
	RefusingCompare() = default;
	RefusingCompare(const RefusingCompare&) {
		if (refuse_copy) throw std::runtime_error("first comparator copy");
	}
	bool operator()(int left, int right) const { return left < right; }
};
template <typename Tree>
void empty_insert_without_comparison() {
	Tree values;
	RefusingCompare::refuse_copy = true;
	if constexpr (std::is_same<typename Tree::key_type, typename Tree::value_type>::value) values.insert(1);
	else values.insert(typename Tree::value_type{1, 1});
	RefusingCompare::refuse_copy = false;
	values.verify(); require(values.size() == 1);
}
template <typename Tree>
void empty_insert_failures() {
	auto state = std::make_shared<Allocations>();
	Tree values(typename Tree::key_compare{}, typename Tree::allocator_type{state});
	auto incoming = [] {
		if constexpr (std::is_same<typename Tree::key_type, typename Tree::value_type>::value)
			return typename Tree::value_type(1);
		else return typename Tree::value_type(1, FirstValue(1));
	}();
	for (bool allocation_failure : {true, false}) {
		state->fail = allocation_failure;
		FirstValue::refuse_copy = !allocation_failure;
		bool refused = false;
		try { values.insert(incoming); }
		catch (const std::bad_alloc&) { require(allocation_failure); refused = true; }
		catch (const std::runtime_error&) { require(!allocation_failure); refused = true; }
		state->fail = false; FirstValue::refuse_copy = false;
		require(refused && values.empty() && values.size() == 0 && state->live.empty());
		values.verify();
	}
	values.insert(incoming); values.verify(); require(values.size() == 1);
	values.clear(); require(state->live.empty());
}
void first_insert_cleanup() {
	empty_insert_without_comparison<btree::map<int, int, RefusingCompare>>();
	empty_insert_without_comparison<btree::multimap<int, int, RefusingCompare>>();
	empty_insert_without_comparison<btree::set<int, RefusingCompare>>();
	empty_insert_without_comparison<btree::multiset<int, RefusingCompare>>();
	using Pair = std::pair<const int, FirstValue>;
	empty_insert_failures<btree::map<int, FirstValue, std::less<int>, CountingAllocator<Pair>>>();
	empty_insert_failures<btree::multimap<int, FirstValue, std::less<int>, CountingAllocator<Pair>>>();
	empty_insert_failures<btree::set<FirstValue, std::less<FirstValue>, CountingAllocator<FirstValue>>>();
	empty_insert_failures<btree::multiset<FirstValue, std::less<FirstValue>, CountingAllocator<FirstValue>>>();
	require(FirstValue::live == 0);
}
struct ExplicitKey {
	ExplicitKey() = delete;
	explicit ExplicitKey(int input) : value(input) {}
	int value;
};
struct ExplicitCompare {
	ExplicitCompare() = delete;
	explicit ExplicitCompare(int) {}
	bool operator()(const ExplicitKey& left, const ExplicitKey& right) const {
		return left.value < right.value;
	}
};
void comparator_expression() {
	btree::map<ExplicitKey, int, ExplicitCompare> values(ExplicitCompare(0));
	values.insert({ExplicitKey(2), 20});
	values.insert({ExplicitKey(1), 10});
	values.verify();
	require(values.find(ExplicitKey(1))->second == 10);
	btree::set<ExplicitKey, ExplicitCompare> keys(ExplicitCompare(0));
	keys.insert(ExplicitKey(2)); keys.insert(ExplicitKey(1));
	keys.verify();
	require(keys.begin()->value == 1 && keys.size() == 2);
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
		if (argc == 2 && std::string_view(argv[1]) == "--short-root-only") {
			short_root_lifetime();
		} else {
			require(argc == 1);
			short_root_lifetime(); compact_roots(); unpadded_leaf_extents(); move_only_values();
			aligned_slots(); allocation_failure(); rejected_alignment(); ordered_model(); range_insertions(); comparator_expression(); first_insert_cleanup();
		}
	}
	catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
	std::cout << checks << " compact layout checks passed\n";
}
