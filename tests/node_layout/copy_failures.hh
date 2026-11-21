// Range insertion and copy/assignment failure positions.
// Internal: included only by tests/node_layout.cc, inside its translation unit.

#pragma once

namespace {
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
template <class Tree>
void check_contents(const Tree& values, int size) {
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
	for (int value = 0; value < 1024; ++value) source.insert(typename decltype(source)::value_type{value % 17, value});
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
template <class T> struct DefaultCountingAllocator : CountingAllocator<T> {
	using CountingAllocator<T>::CountingAllocator;
	DefaultCountingAllocator() : CountingAllocator<T>(std::make_shared<Allocations>()) {}
};
void nested_copy_failure_positions() {
	using Inner = btree::map<int, ThrowingValue, std::less<int>,
		DefaultCountingAllocator<std::pair<const int, ThrowingValue>>>;
	using Entry = std::pair<const int, Inner>;
	using Nested = btree::map<int, Inner, std::less<int>, DefaultCountingAllocator<Entry>>;
	auto state = std::make_shared<Allocations>();
	{
		Nested source(std::less<int>{}, DefaultCountingAllocator<Entry>{state});
		for (int group = 0; group < 4; ++group) {
			Inner inner(std::less<int>{}, DefaultCountingAllocator<std::pair<const int, ThrowingValue>>{state});
			for (int key = 0; key < 40; ++key) inner.emplace(key, key);
			source.emplace(group, inner);
		}
		const auto objects = ThrowingValue::live;
		const auto bytes = state->bytes;
		const auto nodes = state->live.size();
		const auto requests_before = state->requests.size();
		{ Nested copied(source); copied.verify(); }
		const auto requests = state->requests.size() - requests_before;
		for (std::size_t position = 0; position <= requests; ++position) {
			state->remaining = static_cast<int>(position);
			bool refused = false;
			try { Nested copied(source); copied.verify(); }
			catch (const std::bad_alloc&) { refused = true; }
			state->remaining = -1;
			require(refused == (position < requests));
			require(ThrowingValue::live == objects && state->bytes == bytes && state->live.size() == nodes);
			source.verify();
			for (const auto& entry : source) check_contents(entry.second, 40);
		}
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
void aligned_and_duplicate_copies() {
	auto state = std::make_shared<Allocations>();
	{
		using Set = btree::set<AlignedKey, std::less<AlignedKey>, CountingAllocator<AlignedKey>>;
		using Multi = btree::multiset<AlignedKey, std::less<AlignedKey>, CountingAllocator<AlignedKey>>;
		Set unique(std::less<AlignedKey>{}, CountingAllocator<AlignedKey>{state});
		Multi multiple(std::less<AlignedKey>{}, CountingAllocator<AlignedKey>{state});
		for (int value = 0; value < 129; ++value) {
			unique.insert(AlignedKey(value)); multiple.insert(AlignedKey(value % 11));
		}
		auto copied_unique = unique;
		auto copied_multiple = multiple;
		copied_unique.verify(); copied_multiple.verify();
		auto equal = [](const AlignedKey& left, const AlignedKey& right) { return left.value == right.value; };
		require(copied_unique.size() == unique.size() && std::equal(unique.begin(), unique.end(), copied_unique.begin(), equal));
		require(copied_multiple.size() == multiple.size() && std::equal(multiple.begin(), multiple.end(), copied_multiple.begin(), equal));
		for (const auto& value : copied_unique) require(reinterpret_cast<std::uintptr_t>(&value) % alignof(AlignedKey) == 0);
		for (const auto& value : copied_multiple) require(reinterpret_cast<std::uintptr_t>(&value) % alignof(AlignedKey) == 0);
	}
	require(state->live.empty() && state->bytes == 0);
}
}
