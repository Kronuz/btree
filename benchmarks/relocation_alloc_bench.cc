// Heap allocations of string-keyed insertion and erasure, counted at global
// operator new (nodes, key copies and mapped strings together). Compile the
// same source against each compared include tree; run it separately from the
// CPU benchmark because the counting hooks sit on the allocation path.
// usage: relocation_alloc_bench KEY_BYTES
#include <btree/map.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace {
unsigned long calls = 0, bytes = 0;
}
void* operator new(std::size_t size) {
	++calls; bytes += size;
	if (void* pointer = std::malloc(size ? size : 1)) return pointer;
	throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

int main(int argc, char** argv) {
	constexpr unsigned entries = 65536;
	const std::size_t key_bytes = argc == 2 ? std::strtoul(argv[1], nullptr, 10) : 12;
	std::vector<unsigned> order(entries);
	std::iota(order.begin(), order.end(), 0);
	std::mt19937 random(7106);
	std::shuffle(order.begin(), order.end(), random);
	std::vector<std::string> keys;
	for (unsigned i = 0; i < entries; ++i) {
		keys.push_back("group" + std::to_string(1000000 + i));
		keys.back().resize(std::max(key_bytes, keys.back().size()), 'k');
	}
	const std::string value(80, 'v');
	btree::map<std::string, std::string> map;
	const auto insert_calls = calls, insert_bytes = bytes;
	for (auto index : order) map.emplace(keys[index], value);
	const auto inserted_calls = calls - insert_calls, inserted_bytes = bytes - insert_bytes;
	if (map.size() != entries) return 1;
	const auto erase_calls = calls, erase_bytes = bytes;
	for (auto index : order) if (map.erase(keys[index]) != 1) return 1;
	std::printf("{\"key_bytes\":%zu,\"entries\":%u,\"insert_allocation_calls\":%lu,\"insert_requested_bytes\":%lu,\"erase_allocation_calls\":%lu,\"erase_requested_bytes\":%lu}\n",
		keys[0].size(), entries, inserted_calls, inserted_bytes, calls - erase_calls, bytes - erase_bytes);
	return map.empty() ? 0 : 1;
}
