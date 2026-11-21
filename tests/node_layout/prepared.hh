// Prepared-key failures, forwarding, and the ordered model.
// Internal: included only by tests/node_layout.cc, inside its translation unit.

#pragma once

namespace {
struct PreparedKey {
	inline static int copies = -1;
	inline static int live = 0;
	inline static Allocations* deny = nullptr;
	int value;
	explicit PreparedKey(int v) : value(v) { ++live; }
	PreparedKey(const PreparedKey& other) : value(other.value) {
		if (copies == 0) throw std::runtime_error("prepared key copy");
		if (copies > 0) --copies;
		++live;
	}
	PreparedKey(PreparedKey&& other) noexcept : value(other.value) {
		other.value = -999; ++live;
		if (deny) deny->fail = true;
	}
	PreparedKey& operator=(const PreparedKey& other) { value = other.value; return *this; }
	PreparedKey& operator=(PreparedKey&& other) noexcept { value = other.value; other.value = -999; return *this; }
	~PreparedKey() { --live; }
};
struct PreparedCompare {
	bool reverse = false;
	bool operator()(const PreparedKey& a, const PreparedKey& b) const {
		return reverse ? a.value > b.value : a.value < b.value;
	}
};
void prepared_key_failures() {
	using Pair = std::pair<const PreparedKey, int>;
	using Params = btree::btree_map_params<PreparedKey, int, PreparedCompare, CountingAllocator<Pair>, 256>;
	using Node = btree::btree_node<Params>;
	using Tree = btree::map<PreparedKey, int, PreparedCompare, CountingAllocator<Pair>>;
	static_assert(std::is_nothrow_move_constructible<PreparedKey>::value);
	static_assert(!std::is_nothrow_copy_constructible<PreparedKey>::value);
	for (bool reverse : {false, true}) for (int count : {3, 160}) {
		int covered = 0;
		for (int key = -1; key < count * 2; key += 2) {
			auto state = std::make_shared<Allocations>();
			{
				Tree values(PreparedCompare{reverse}, CountingAllocator<Pair>{state});
				std::vector<int> order;
				for (int i = 0; i < count; ++i) order.push_back(i * 2);
				std::mt19937 random(1093); std::shuffle(order.begin(), order.end(), random);
				for (int item : order) values.try_emplace(PreparedKey(item), item);
				auto cursor = values.lower_bound(PreparedKey(key));
				if (cursor == values.end()) continue;
				Node* node = nullptr;
				int position = -1;
				auto address = reinterpret_cast<std::uintptr_t>(std::addressof(*cursor));
				for (const auto& allocation : state->live) {
					auto begin = reinterpret_cast<std::uintptr_t>(allocation.first);
					if (address >= begin && address < begin + allocation.second.bytes) node = std::launder(static_cast<Node*>(allocation.first));
				}
				require(node != nullptr);
				if (!node->is_leaf() || node->count() == node->max_count()) continue;
				for (int i = 0; i < node->count(); ++i) if (std::addressof(node->value(i)) == std::addressof(*cursor)) position = i;
				require(position >= 0);
				++covered;
				const auto bytes = state->bytes, calls = state->requests.size();
				// Shifting relocates keys by move: refusing every copy and every allocation
				// must not matter, and nothing is allocated or copied.
				PreparedKey::copies = 0;
				PreparedKey::deny = state.get();
				auto inserted = values.try_emplace(PreparedKey(key), key);
				PreparedKey::deny = nullptr; state->fail = false; PreparedKey::copies = -1;
				require(inserted.second && inserted.first->first.value == key && inserted.first->second == key);
				require(state->requests.size() == calls && state->bytes == bytes && values.size() == static_cast<std::size_t>(count + 1));
				values.verify();
			}
			require(PreparedKey::live == 0 && state->live.empty() && state->bytes == 0);
		}
		require(covered > 0);
	}
	btree::map<int, std::string> values;
	values.emplace(0, "zero"); values.emplace(4, "four"); values.emplace(8, "eight");
	values.try_emplace(2, values.at(4));
	require(values.at(2) == "four" && values.at(4) == "four");
	std::pair<int, std::string> pair{3, "three"};
	values.emplace(pair); require(pair.second == "three" && values.at(3) == "three");
	values.emplace(std::pair<int, std::string>{5, "five"});
	values.emplace(std::piecewise_construct, std::forward_as_tuple(6), std::forward_as_tuple(3, 'x'));
	require(values.at(5) == "five" && values.at(6) == "xxx"); values.verify();
}

struct ForwardedValue {
	int value;
	explicit ForwardedValue(int v) : value(v) {}
	ForwardedValue(const ForwardedValue&) = default;
	ForwardedValue(ForwardedValue&& other) noexcept : value(other.value) { other.value = -1; }
	ForwardedValue(const ForwardedValue&&) = delete;
	ForwardedValue& operator=(const ForwardedValue&) = default;
	ForwardedValue& operator=(ForwardedValue&&) = default;
};
struct PlacementKey {
	int value;
	explicit PlacementKey(int v) : value(v) {}
	PlacementKey(const PlacementKey& other) noexcept(false) : value(other.value) {}
	PlacementKey(PlacementKey&&) noexcept = default;
	PlacementKey& operator=(const PlacementKey&) = default;
	PlacementKey& operator=(PlacementKey&&) = default;
	static void* operator new(std::size_t) = delete;
	bool operator<(const PlacementKey& other) const { return value < other.value; }
};
struct BudgetKey {
	unsigned char bytes[3];
	explicit BudgetKey(unsigned char value) : bytes{value, 0, 0} {}
	BudgetKey(const BudgetKey& other) noexcept(false) : bytes{other.bytes[0], 0, 0} {}
	BudgetKey(BudgetKey&&) noexcept = default;
	BudgetKey& operator=(const BudgetKey&) = default;
	BudgetKey& operator=(BudgetKey&&) = default;
	bool operator<(const BudgetKey& other) const { return bytes[0] < other.bytes[0]; }
};
struct BudgetMapped {
	unsigned char bytes[243]{};
	explicit BudgetMapped(unsigned char v) { bytes[0] = v; }
};
void prepared_forwarding() {
	for (int form = 0; form < 4; ++form) {
		btree::map<int, ForwardedValue> values;
		for (int key : {0, 4, 8}) values.emplace(key, ForwardedValue(key));
		const ForwardedValue input(2);
		if (form == 0) values.emplace(2, std::move(input));
		if (form == 1) {
			const std::pair<int, ForwardedValue> pair{2, ForwardedValue(2)};
			values.emplace(std::move(pair));
		}
		if (form == 2) {
			std::pair<int, const ForwardedValue> pair{2, ForwardedValue(2)};
			values.emplace(std::move(pair));
		}
		if (form == 3) {
			int key = 2; ForwardedValue mapped(2);
			std::pair<int&&, ForwardedValue&&> pair{std::move(key), std::move(mapped)};
			values.emplace(std::move(pair)); require(mapped.value == -1);
		}
		require(values.at(2).value == 2 && values.at(4).value == 4 && input.value == 2); values.verify();
	}
	{
		btree::map<PlacementKey, int> values;
		for (int key : {0, 4, 8, 2}) values.emplace(PlacementKey(key), key);
		require(values.at(PlacementKey(2)) == 2); values.verify();
	}
	{
		int zero = 0, four = 4, eight = 8, two = 2;
		btree::map<int, int&> values;
		values.emplace(0, zero); values.emplace(4, four); values.emplace(8, eight); values.emplace(2, two);
		require(&values.at(2) == &two && &values.at(4) == &four && four == 4 && eight == 8); values.verify();
	}
	using Pair = std::pair<const BudgetKey, BudgetMapped>;
	using Params = btree::btree_map_params<BudgetKey, BudgetMapped, std::less<BudgetKey>, CountingAllocator<Pair>, 256>;
	using Node = btree::btree_node<Params>;
	static_assert(sizeof(BudgetKey) == 3 && alignof(BudgetKey) == 1 && sizeof(BudgetMapped) == 243);
	auto state = std::make_shared<Allocations>();
	{
		btree::map<BudgetKey, BudgetMapped, std::less<BudgetKey>, CountingAllocator<Pair>> values(std::less<BudgetKey>{}, CountingAllocator<Pair>{state});
		for (unsigned char key : {0, 4, 8, 12}) values.try_emplace(BudgetKey(key), key);
		auto last = values.find(BudgetKey(12)); require(last != values.end());
		Node* node = nullptr;
		auto address = reinterpret_cast<std::uintptr_t>(std::addressof(*last));
		for (const auto& allocation : state->live) {
			auto begin = reinterpret_cast<std::uintptr_t>(allocation.first);
			if (address >= begin && address < begin + allocation.second.bytes) node = std::launder(static_cast<Node*>(allocation.first));
		}
		require(node && node->is_leaf() && node->count() < node->max_count());
		// The pair fits the node-size budget and its moves cannot throw, so a
		// middle insertion into a nonfull leaf needs no allocation.
		const auto calls = state->requests.size();
		state->fail = true;
		values.try_emplace(BudgetKey(10), 10);
		state->fail = false;
		require(values.size() == 5 && values.at(BudgetKey(10)).bytes[0] == 10 && state->requests.size() == calls); values.verify();
	}
	require(state->live.empty() && state->bytes == 0);
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
