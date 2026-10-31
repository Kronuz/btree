// Allocation failure during structural mutation with heap-allocated keys.
//
// Map slots store a mutable std::pair<Key, T>, so splitting, rebalancing,
// merging, erasing and collapsing the root relocate keys by move. Keys longer
// than the small-string buffer would allocate on every copy. This fixture
// refuses allocations at every point of an insertion, requires the tree to be
// unchanged after each refusal, and requires every erase to complete with the
// heap closed, so relocation itself allocates nothing.
#include "btree/map.h"
#include "btree/set.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <new>
#include <random>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace {
long allocation_budget = -1;
long live_allocations = 0;
long allocation_calls = 0;
void admit_allocation() {
	if (allocation_budget == 0) throw std::bad_alloc();
	if (allocation_budget > 0) --allocation_budget;
	++allocation_calls;
}
void check(bool condition, const char* what, int line) {
	if (condition) return;
	std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
	std::abort();
}
#define CHECK(condition) check((condition), #condition, __LINE__)
}

void* operator new(std::size_t bytes) {
	admit_allocation();
	if (void* pointer = std::malloc(bytes ? bytes : 1)) { ++live_allocations; return pointer; }
	throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { if (pointer) { --live_allocations; std::free(pointer); } }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

namespace {
// Longer than any small-string buffer, so every key copy allocates.
std::string long_key(unsigned number, std::size_t width = 80) {
	char digits[16];
	std::snprintf(digits, sizeof digits, "%08u", number);
	std::string key(width, 'k');
	key.replace(width - 8, 8, digits);
	return key;
}

enum class Order { ascending, descending, shuffled };
std::vector<unsigned> schedule(unsigned count, Order order) {
	std::vector<unsigned> keys(count);
	for (unsigned i = 0; i < count; ++i) keys[i] = i * 2 + 1;
	if (order == Order::descending) std::reverse(keys.begin(), keys.end());
	if (order == Order::shuffled) { std::mt19937 random(count * 7 + 1); std::shuffle(keys.begin(), keys.end(), random); }
	return keys;
}

template <class Tree, class Model>
void same_contents(const Tree& tree, const Model& model) {
	CHECK(tree.size() == model.size());
	auto expected = model.begin();
	for (const auto& entry : tree) {
		CHECK(expected != model.end());
		if constexpr (std::is_same<Model, std::set<std::string>>::value) CHECK(entry == *expected);
		else CHECK(entry.first == expected->first && entry.second == expected->second);
		++expected;
	}
	CHECK(expected == model.end());
	tree.verify();
}

// One insertion with the n-th allocation refused, for n = 0, 1, ... until it
// succeeds. Each refusal leaves the contents, the structure and the live
// allocations exactly as they were.
template <class Tree, class Model, class Insert, class Add>
void insert_with_refusals(Tree& tree, Model& model, Insert insert, Add add, unsigned& refusals) {
	const long before = live_allocations;
	for (long budget = 0;; ++budget) {
		allocation_budget = budget;
		bool refused = false;
		try { insert(tree); } catch (const std::bad_alloc&) { refused = true; }
		allocation_budget = -1;
		if (!refused) { add(model); break; }
		++refusals;
		if (live_allocations != before) std::fprintf(stderr, "budget %ld size %zu live %ld before %ld\n", budget, tree.size(), live_allocations, before);
		CHECK(live_allocations == before);
		same_contents(tree, model);
		CHECK(budget < 64);
	}
	same_contents(tree, model);
}

template <class Tree, class Model, class Erase>
void erase_with_closed_heap(Tree& tree, Model& model, Erase erase) {
	allocation_budget = 0;
	allocation_calls = 0;
	bool refused = false;
	try { erase(tree); } catch (const std::bad_alloc&) { refused = true; }
	allocation_budget = -1;
	CHECK(!refused && allocation_calls == 0);
	erase(model);
	same_contents(tree, model);
}

void map_insertion_and_erasure(Order order, std::size_t width, unsigned count) {
	const long baseline = live_allocations;
	unsigned refusals = 0;
	{
		btree::map<std::string, std::string> tree;
		std::map<std::string, std::string> model;
		for (unsigned key : schedule(count, order)) {
			const auto k = long_key(key, width);
			const auto v = long_key(key + 1, width + 8);
			insert_with_refusals(tree, model, [&](auto& t) { t.emplace(k, v); },
				[&](auto& m) { m.emplace(k, v); }, refusals);
		}
		// Erase in a different order from the one used to build the tree.
		auto keys = schedule(count, order == Order::shuffled ? Order::ascending : Order::shuffled);
		for (unsigned key : keys) {
			const auto k = long_key(key, width);
			erase_with_closed_heap(tree, model, [&](auto& t) { CHECK(t.erase(k) == 1); });
		}
		CHECK(tree.empty());
		// Reuse after collapse to an empty tree.
		const auto k = long_key(1, width);
		tree.emplace(k, k);
		tree.verify();
	}
	CHECK(live_allocations == baseline);
	CHECK(refusals > 0);
}

void set_insertion_and_erasure(Order order, unsigned count) {
	const long baseline = live_allocations;
	unsigned refusals = 0;
	{
		btree::set<std::string> tree;
		std::set<std::string> model;
		for (unsigned key : schedule(count, order)) {
			const auto k = long_key(key);
			insert_with_refusals(tree, model, [&](auto& t) { t.insert(k); }, [&](auto& m) { m.insert(k); }, refusals);
		}
		for (unsigned key : schedule(count, Order::shuffled)) {
			const auto k = long_key(key);
			erase_with_closed_heap(tree, model, [&](auto& t) { t.erase(k); });
		}
		CHECK(tree.empty());
	}
	CHECK(live_allocations == baseline);
	CHECK(refusals > 0);
}

// Erase through iterators, which takes the internal-node swap path.
void iterator_erasure(unsigned count) {
	const long baseline = live_allocations;
	{
		btree::map<std::string, std::string> tree;
		std::map<std::string, std::string> model;
		for (unsigned key : schedule(count, Order::shuffled)) {
			tree.emplace(long_key(key), long_key(key + 1));
			model.emplace(long_key(key), long_key(key + 1));
		}
		std::mt19937 random(11);
		while (!tree.empty()) {
			auto offset = random() % tree.size();
			auto position = tree.begin();
			std::advance(position, offset);
			const auto k = position->first;
			allocation_budget = 0;
			allocation_calls = 0;
			auto next = tree.erase(position);
			allocation_budget = -1;
			CHECK(allocation_calls == 0);
			model.erase(k);
			CHECK((next == tree.end()) == (model.lower_bound(k) == model.end()));
			if (next != tree.end()) CHECK(next->first == model.lower_bound(k)->first);
			if (tree.size() % 16 == 0) same_contents(tree, model);
		}
		tree.verify();
	}
	CHECK(live_allocations == baseline);
}

// Maps with a move-only key work: keys are relocated, never copied. The
// revert check builds the fixture against a header that cannot compile this.
#ifndef RELOCATION_SKIP_MOVE_ONLY_KEYS
struct Indirect {
	bool operator()(const std::unique_ptr<int>& a, const std::unique_ptr<int>& b) const { return *a < *b; }
};
void move_only_keys() {
	btree::map<std::unique_ptr<int>, std::string, Indirect> tree;
	std::vector<int> keys(300);
	for (int i = 0; i < 300; ++i) keys[i] = i;
	std::mt19937 random(5);
	std::shuffle(keys.begin(), keys.end(), random);
	for (int key : keys) tree.emplace(std::make_unique<int>(key), long_key(key));
	tree.verify();
	int expected = 0;
	for (const auto& entry : tree) CHECK(*entry.first == expected++ && entry.second == long_key(*entry.first));
	for (int key : keys) CHECK(tree.erase(std::make_unique<int>(key)) == 1);
	CHECK(tree.empty());
}
#endif

void layout_contract() {
	using Pair = std::pair<const std::string, std::string>;
	using Mutable = std::pair<std::string, std::string>;
	static_assert(sizeof(Pair) == sizeof(Mutable) && alignof(Pair) == alignof(Mutable));
	static_assert(std::is_nothrow_move_constructible<Mutable>::value);
	static_assert(std::is_nothrow_move_constructible<btree::map<std::string, std::string>>::value);
	static_assert(std::is_same<btree::map<std::string, int>::value_type, std::pair<const std::string, int>>::value);
}
}

int main() {
	std::setvbuf(stderr, nullptr, _IONBF, 0);
	const long start = live_allocations;
	layout_contract();
	for (Order order : {Order::ascending, Order::descending, Order::shuffled}) {
		map_insertion_and_erasure(order, 80, 400);
		map_insertion_and_erasure(order, 40, 120);
		set_insertion_and_erasure(order, 300);
	}
	iterator_erasure(300);
#ifndef RELOCATION_SKIP_MOVE_ONLY_KEYS
	move_only_keys();
#endif
	CHECK(live_allocations == start);
	std::puts("PASS relocation under allocation failure: unchanged after every refusal, erase allocation-free");
}
