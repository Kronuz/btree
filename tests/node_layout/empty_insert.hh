// Empty-tree insertion, comparator expressions and range insertions.
// Internal: included only by tests/node_layout.cc, inside its translation unit.

#pragma once

namespace {
int int_key(int number) { return number; }
std::string long_key(int number) { return std::string(64, 'k') + char('B' + number); }
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
}
