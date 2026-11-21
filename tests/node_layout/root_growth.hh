// Root growth, nonfull leaf and append failures, and move-only recovery.
// Internal: included only by tests/node_layout.cc, inside its translation unit.

#pragma once

namespace {
struct RootOrder {
	bool reverse = false;
	bool operator()(int left, int right) const { return reverse ? left > right : left < right; }
};
void root_growth_failures() {
	using Pair = std::pair<const int, ThrowingValue>;
	using Tree = btree::map<int, ThrowingValue, RootOrder, CountingAllocator<Pair>>;
	using Params = btree::btree_map_params<int, ThrowingValue, RootOrder, CountingAllocator<Pair>, 256>;
	constexpr int capacity = btree::btree_node<Params>::kNodeValues;
	for (bool reverse : {false, true}) for (int count = 1; count < capacity; count = std::min(capacity, count * 2)) {
		for (int position = 0; position <= count; ++position) {
			auto state = std::make_shared<Allocations>();
			{
				Tree values(RootOrder{reverse}, CountingAllocator<Pair>{state});
				for (int key = 0; key < count; ++key) values.try_emplace(key * 2, key * 2);
				const int key = (reverse ? count - position : position) * 2 - 1;
				ThrowingValue incoming(key);
				const auto objects = ThrowingValue::live;
				const auto bytes = state->bytes;
				const auto nodes = state->live.size();
				std::vector<std::pair<int, int>> original;
				for (const auto& value : values) original.emplace_back(value.first, value.second.value);
				auto unchanged = [&] {
					require(values.size() == static_cast<std::size_t>(count));
					auto expected = original.begin();
					for (const auto& value : values) {
						require(value.first == expected->first && value.second.value == expected->second); ++expected;
					}
					require(ThrowingValue::live == objects && state->bytes == bytes && state->live.size() == nodes);
					values.verify();
				};
				state->remaining = 0;
				bool allocation_refused = false;
				try { values.try_emplace(key, incoming); } catch (const std::bad_alloc&) { allocation_refused = true; }
				state->remaining = -1;
				require(allocation_refused); unchanged();
				for (int budget = 0; budget <= count + 1; ++budget) {
					ThrowingValue::remaining = budget;
					bool refused = false;
					try { values.try_emplace(key, incoming); }
					catch (const std::runtime_error& error) {
						if (std::string_view(error.what()) != "positioned value copy") throw;
						refused = true;
					}
					ThrowingValue::remaining = -1;
					require(refused == (budget < count + 1));
					if (refused) unchanged();
				}
				values.verify(); require(values.size() == static_cast<std::size_t>(count + 1));
				require(values.find(key)->second.value == key);
			}
			require(state->live.empty() && state->bytes == 0 && ThrowingValue::live.empty());
		}
	}
}
using NonfullPair = std::pair<const int, ThrowingValue>;
using NonfullNode = btree::btree_node<btree::btree_map_params<int, ThrowingValue, RootOrder, CountingAllocator<NonfullPair>, 256>>;
template <class Iterator>
NonfullNode* nonfull_node(const Iterator& cursor, const Allocations& state) {
	const auto address = reinterpret_cast<std::uintptr_t>(std::addressof(*cursor));
	for (const auto& allocation : state.live) {
		const auto start = reinterpret_cast<std::uintptr_t>(allocation.first);
		if (address >= start && address - start < allocation.second.bytes)
			return std::launder(static_cast<NonfullNode*>(allocation.first));
	}
	throw std::runtime_error("value outside tracked node allocation");
}
template <class Iterator>
int nonfull_position(const Iterator& cursor, const Allocations& state) {
	auto* node = nonfull_node(cursor, state);
	for (int i = 0; i < node->count(); ++i)
		if (std::addressof(node->value(i)) == std::addressof(*cursor)) return i;
	throw std::runtime_error("value outside active node prefix");
}
void nonfull_leaf_failures() {
	using Pair = std::pair<const int, ThrowingValue>;
	using Tree = btree::map<int, ThrowingValue, RootOrder, CountingAllocator<Pair>>;
	for (bool reverse : {false, true}) for (int topology = 0; topology < 6; ++topology) {
		bool covered = false;
		for (unsigned seed = 0; seed < 64 && !covered; ++seed) {
			auto state = std::make_shared<Allocations>();
			{
				Tree values(RootOrder{reverse}, CountingAllocator<Pair>{state});
				const int count = topology == 0 ? 3 : 160;
				std::vector<int> order;
				for (int i = 0; i < count; ++i) order.push_back(i * 2);
				std::mt19937 random(7106 + seed);
				std::shuffle(order.begin(), order.end(), random);
				for (int key : order) values.try_emplace(key, key);
				auto first = nonfull_node(values.begin(), *state);
				auto tail = values.end(); --tail;
				auto last = nonfull_node(tail, *state);
				for (int key = -1; key < count * 2 && !covered; key += 2) {
					auto slot = values.lower_bound(key);
					if (slot == values.end() || !nonfull_node(slot, *state)->is_leaf() || nonfull_position(slot, *state) == nonfull_node(slot, *state)->count() || nonfull_node(slot, *state)->count() == nonfull_node(slot, *state)->max_count()) continue;
					auto* original = nonfull_node(slot, *state);
					const bool root_leaf = original->is_root();
					if ((topology == 0) != root_leaf) continue;
					if (topology == 1 && original != first) continue;
					if (topology == 2 && original != last) continue;
					if (topology >= 3) {
						if (original == first || original == last) continue;
						const int child = original->position(), children = original->get_parent()->count();
						if (topology == 3 && child != 0) continue;
						if (topology == 4 && child != children) continue;
						if (topology == 5 && (child == 0 || child == children)) continue;
					}
					covered = true;
					ThrowingValue incoming(key);
					const auto objects = ThrowingValue::live;
					const auto bytes = state->bytes, nodes = state->live.size();
					const int leaf_count = original->count();
					std::vector<std::pair<int, int>> contents;
					for (const auto& entry : values) contents.emplace_back(entry.first, entry.second.value);
					auto unchanged = [&] {
						require(values.size() == static_cast<std::size_t>(count));
						auto expected = contents.begin();
						for (const auto& entry : values) {
							require(expected != contents.end() && entry.first == expected->first && entry.second.value == expected->second); ++expected;
						}
						require(expected == contents.end() && nonfull_node(values.begin(), *state) == first);
						auto end = values.end(); --end; require(nonfull_node(end, *state) == last);
						require(original->count() == leaf_count && ThrowingValue::live == objects && state->bytes == bytes && state->live.size() == nodes);
						if (!root_leaf) require(original->get_parent()->child(original->position()) == original);
						values.verify();
					};
					state->remaining = 0;
					bool refused = false;
					try { values.try_emplace(key, incoming); } catch (const std::bad_alloc&) { refused = true; }
					state->remaining = -1; require(refused); unchanged();
					for (int budget = 0; budget <= leaf_count + 1; ++budget) {
						ThrowingValue::remaining = budget; refused = false;
						try { values.try_emplace(key, incoming); } catch (const std::runtime_error&) { refused = true; }
						ThrowingValue::remaining = -1;
						require(refused == (budget < leaf_count + 1));
						if (refused) unchanged();
					}
					values.verify(); require(values.size() == static_cast<std::size_t>(count + 1));
					require(values.at(key).value == key);
				}
			}
			require(ThrowingValue::live.empty() && state->live.empty() && state->bytes == 0);
		}
		require(covered);
	}
}
void nonfull_append_failure() {
	using Pair = std::pair<const int, ThrowingValue>;
	using Tree = btree::map<int, ThrowingValue, RootOrder, CountingAllocator<Pair>>;
	for (bool reverse : {false, true}) {
		auto state = std::make_shared<Allocations>();
		{
			Tree values(RootOrder{reverse}, CountingAllocator<Pair>{state});
			std::vector<int> keys;
			for (int i = 0; i < 160; ++i) keys.push_back(i * 2);
			std::mt19937 random(7106); std::shuffle(keys.begin(), keys.end(), random);
			for (int key : keys) values.try_emplace(key, key);
			bool covered = false;
			for (auto entry = values.begin(); entry != values.end() && !covered; ++entry) {
				if (!nonfull_node(entry, *state)->is_leaf() || nonfull_node(entry, *state)->is_root() || nonfull_position(entry, *state) != nonfull_node(entry, *state)->count() - 1 || nonfull_node(entry, *state)->count() == nonfull_node(entry, *state)->max_count()) continue;
				const int key = entry->first + (reverse ? -1 : 1);
				auto next = entry; ++next;
				if (next != values.end() && nonfull_node(next, *state) == nonfull_node(entry, *state)) continue;
				covered = true;
				auto* original = nonfull_node(entry, *state);
				ThrowingValue incoming(key);
				const auto size = values.size();
				const auto bytes = state->bytes, nodes = state->live.size();
				const auto objects = ThrowingValue::live;
				ThrowingValue::remaining = 0; state->remaining = 0;
				bool refused = false;
				try { values.try_emplace(key, incoming); } catch (const std::runtime_error&) { refused = true; }
				ThrowingValue::remaining = -1;
				if (values.size() != size) std::cerr << "refused nonfull append changes size " << size << " -> " << values.size() << "\n";
				require(refused && values.size() == size && ThrowingValue::live == objects);
				require(state->bytes == bytes && state->live.size() == nodes); values.verify();
				auto inserted = values.try_emplace(key, incoming);
				state->remaining = -1;
				require(inserted.second && nonfull_node(inserted.first, *state) == original && values.size() == size + 1);
				require(state->bytes == bytes && state->live.size() == nodes); values.verify();
				break;
			}
			require(covered);
		}
		require(ThrowingValue::live.empty() && state->live.empty() && state->bytes == 0);
	}
}
struct RootIncoming {
	inline static bool refuse = false;
	inline static int moves = 0;
	int value;
	explicit RootIncoming(int number) : value(number) {
		if (refuse) throw std::runtime_error("incoming refused");
	}
	RootIncoming(const RootIncoming&) = default;
	RootIncoming& operator=(const RootIncoming&) = default;
	RootIncoming(RootIncoming&& other) noexcept : value(other.value) { other.value = -1; ++moves; }
	RootIncoming& operator=(RootIncoming&&) = default;
};
void root_growth_aliases() {
	for (int key : {-1, 1}) {
		btree::map<int, std::string> values;
		values.try_emplace(0, std::string(80, 'x'));
		values.try_emplace(key, values.begin()->second);
		values.verify(); require(values.size() == 2);
		require(values.at(0) == std::string(80, 'x') && values.at(key) == std::string(80, 'x'));
		btree::map<int, std::string> moved;
		moved.try_emplace(0, std::string(80, 'x'));
		moved.try_emplace(key, std::move(moved.begin()->second));
		moved.verify(); require(moved.at(key) == std::string(80, 'x'));
	}
	btree::map<int, RootIncoming> values;
	values.try_emplace(0, 10);
	RootIncoming::moves = 0; RootIncoming::refuse = true;
	bool refused = false;
	try { values.try_emplace(1, 20); } catch (const std::runtime_error&) { refused = true; }
	RootIncoming::refuse = false;
	require(refused && RootIncoming::moves == 0 && values.size() == 1 && values.at(0).value == 10);
	values.verify(); values.try_emplace(1, 20); values.verify();
	btree::multimap<int, int> multiple;
	for (int number = 0; number < 16; ++number) multiple.insert({3, number});
	multiple.verify(); int number = 0;
	for (const auto& value : multiple) require(value.first == 3 && value.second == number++);
}
struct RootMoveKey {
	inline static int live = 0, budget = -1;
	int value;
	explicit RootMoveKey(int number) : value(number) { ++live; }
	RootMoveKey(const RootMoveKey&) = delete;
	RootMoveKey(RootMoveKey&& other) : value(other.value) {
		other.value = -1;
		if (budget == 0) throw std::runtime_error("key move refused");
		if (budget > 0) --budget;
		++live;
	}
	RootMoveKey& operator=(RootMoveKey&& other) {
		value = other.value; other.value = -1;
		if (budget == 0) throw std::runtime_error("key move refused");
		if (budget > 0) --budget;
		return *this;
	}
	~RootMoveKey() { --live; }
	bool operator<(const RootMoveKey& other) const { return value < other.value; }
};
void root_growth_move_only_recovery() {
	for (int count : {1, 2, 4}) for (int position = 0; position <= count; ++position) {
		for (int budget = 0; budget <= count + 1; ++budget) {
			{
				btree::set<RootMoveKey> values;
				for (int index = 0; index < count; ++index) values.insert(RootMoveKey((index + 1) * 4));
				RootMoveKey incoming((position + 1) * 4 - 2);
				RootMoveKey::budget = budget;
				bool refused = false;
				try { values.insert(std::move(incoming)); } catch (const std::runtime_error&) { refused = true; }
				RootMoveKey::budget = -1;
				require(refused == (budget <= count));
				if (!refused) require(values.size() == static_cast<std::size_t>(count + 1));
				else if (budget == 0) {
					require(values.size() == static_cast<std::size_t>(count));
					int index = 0;
					for (const auto& value : values) require(value.value == ++index * 4);
				} else require(values.empty());
				values.verify(); values.insert(RootMoveKey(999)); values.verify();
			}
			require(RootMoveKey::live == 0);
		}
	}
}
// A throwing move-only mapped value during compact-root growth leaves every key
// in place, with heap-allocated keys as with trivially copyable ones.
template <typename Key>
void root_growth_move_only_mapped(Key (*make)(int)) {
	using Pair = std::pair<const Key, RootMoveKey>;
	using Tree = btree::map<Key, RootMoveKey, std::less<Key>, CountingAllocator<Pair>>;
	for (int key : {-1, 1}) {
		auto state = std::make_shared<Allocations>();
		{
			Tree values(std::less<Key>{}, CountingAllocator<Pair>{state});
			values.try_emplace(make(0), 5);
			const auto bytes = state->bytes;
			RootMoveKey::budget = 0;
			bool refused = false;
			try { values.try_emplace(make(key), 7); } catch (const std::runtime_error&) { refused = true; }
			RootMoveKey::budget = -1;
			require(refused && values.size() == 1 && values.begin()->first == make(0));
			require(values.begin()->second.value == -1 && RootMoveKey::live == 1);
			require(state->bytes == bytes && state->live.size() == 1);
			values.verify(); values.try_emplace(make(key), 9); values.verify();
		}
		require(state->live.empty() && state->bytes == 0 && RootMoveKey::live == 0);
	}
}
}
