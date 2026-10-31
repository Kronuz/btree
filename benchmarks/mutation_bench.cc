// Compile with -O3 and either -DNDEBUG or -UNDEBUG. Repeat paired builds
// against the two include trees without concurrent builds or tests.
#include <algorithm>
#include <btree/map.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#ifndef BTREE_BENCH_CYCLES
#define BTREE_BENCH_CYCLES 4
#endif

int main(int argc, char** argv) {
	constexpr unsigned entries = 65536, cycles = BTREE_BENCH_CYCLES;
	std::vector<std::string> keys;
	std::vector<unsigned> order(entries);
	std::iota(order.begin(), order.end(), 0);
	std::mt19937 random(7106);
	std::shuffle(order.begin(), order.end(), random);
	for (unsigned i = 0; i < entries; ++i)
		keys.push_back("group" + std::to_string(1000000 + i));
	if (argc == 2 && std::string_view(argv[1]) == "--move-nested") {
		using Groups = btree::map<std::string, std::string>;
		btree::map<std::string, Groups> source;
		constexpr unsigned nested_entries = 4096, groups = 8;
		for (unsigned index = 0; index < nested_entries; ++index) {
			Groups values;
			for (unsigned group = 0; group < groups; ++group)
				values.emplace("host-group:" + std::string(32, 'g') + std::to_string(group), std::string(80, 'v'));
			source.emplace("ranking:feature:" + keys[index] + std::string(24, 'k'), std::move(values));
		}
		const auto start = std::clock();
		for (unsigned cycle = 0; cycle < cycles; ++cycle) {
			auto moved = std::move(source);
			source = std::move(moved);
		}
		const auto elapsed = 1000.0 * (std::clock() - start) / CLOCKS_PER_SEC;
		if (source.size() != nested_entries) return 1;
		for (const auto& entry : source) if (entry.second.size() != groups) return 1;
		std::printf("{\"workload\":\"move-nested\",\"entries\":%u,\"groups\":%u,\"cycles\":%u,\"process_cpu_ms\":%.6f}\n",
			nested_entries, groups, cycles, elapsed);
		return 0;
	}
	if (argc == 2 && (std::string_view(argv[1]) == "--copy" || std::string_view(argv[1]) == "--copy-pruned")) {
		btree::map<std::string, std::string> source;
		for (auto index : order) source.emplace(keys[index], keys[index] + std::string(80, 'x'));
		const bool pruned = std::string_view(argv[1]) == "--copy-pruned";
		if (pruned) for (auto index : order) if (index % 4) source.erase(keys[index]);
		std::size_t copied_bytes = 0;
		const auto start = std::clock();
		for (unsigned cycle = 0; cycle < cycles; ++cycle) {
			auto copied = source;
			if (copied.size() != source.size() || !std::equal(source.begin(), source.end(), copied.begin())) return 1;
			copied_bytes = copied.bytes_used();
		}
		std::printf("{\"workload\":\"%s\",\"entries\":%zu,\"cycles\":%u,\"source_container_bytes\":%zu,\"copy_container_bytes\":%zu,\"process_cpu_ms\":%.6f}\n",
			pruned ? "copy-pruned" : "copy", static_cast<std::size_t>(source.size()), cycles,
			static_cast<std::size_t>(source.bytes_used()), copied_bytes, 1000.0 * (std::clock() - start) / CLOCKS_PER_SEC);
		return 0;
	}
	// --keys=N pads each key to N bytes: 16 and 200 bytes exceed small-string
	// buffers, so every key copy allocates and every key move does not.
	std::size_t key_bytes = 0;
	if (argc == 2 && std::string_view(argv[1]).substr(0, 7) == "--keys=")
		key_bytes = std::strtoul(argv[1] + 7, nullptr, 10);
	else if (argc != 1) return 2;
	if (key_bytes)
		for (auto& key : keys) key.resize(std::max(key_bytes, key.size()), 'k');
	const auto start = std::clock();
	for (unsigned cycle = 0; cycle < cycles; ++cycle) {
		btree::map<std::string, std::string> map;
		for (auto index : order)
			map.emplace(keys[index], keys[index] + std::string(80, 'x'));
		for (auto index : order)
			if (map.erase(keys[index]) != 1)
				return 1;
		if (!map.empty())
			return 1;
	}
	std::printf("{\"entries\":%u,\"cycles\":%u,\"key_bytes\":%zu,\"process_cpu_ms\":%.6f}\n", entries, cycles,
				keys[0].size(), 1000.0 * (std::clock() - start) / CLOCKS_PER_SEC);
}
