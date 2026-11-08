// Compile with -O3 and either -DNDEBUG or -UNDEBUG. Repeat paired builds
// against the two include trees without concurrent builds or tests.
#include <algorithm>
#include <btree/map.h>
#include <cstdio>
#include <ctime>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
	constexpr unsigned entries = 65536, cycles = 4;
	std::vector<std::string> keys;
	std::vector<unsigned> order(entries);
	std::iota(order.begin(), order.end(), 0);
	std::mt19937 random(7106);
	std::shuffle(order.begin(), order.end(), random);
	for (unsigned i = 0; i < entries; ++i)
		keys.push_back("group" + std::to_string(1000000 + i));
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
	if (argc != 1) return 2;
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
	std::printf("{\"entries\":%u,\"cycles\":%u,\"process_cpu_ms\":%.6f}\n", entries, cycles,
				1000.0 * (std::clock() - start) / CLOCKS_PER_SEC);
}
