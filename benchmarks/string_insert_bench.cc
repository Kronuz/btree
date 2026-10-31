// std::string-keyed insert/find: btree::map vs absl::btree_map. ns per operation,
// best of 5. Build against an older header tree (-I) for a before/after.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include <absl/container/btree_map.h>

#include "btree/map.h"

static volatile std::uint64_t sink;

template <class F>
static double best_ns(F&& f, double ops, int reps = 5) {
	f();
	double best = 1e18;
	for (int i = 0; i < reps; ++i) {
		auto a = std::chrono::steady_clock::now();
		f();
		auto b = std::chrono::steady_clock::now();
		best = std::min(best, std::chrono::duration<double, std::nano>(b - a).count() / ops);
	}
	return best;
}

template <class M>
static void row(const char* name, const std::vector<std::string>& keys, const std::vector<std::string>& sorted) {
	const double n = double(keys.size());
	double rnd = best_ns([&] { M m; for (std::size_t i = 0; i < keys.size(); ++i) m.emplace(keys[i], int(i)); sink = m.size(); }, n);
	double srt = best_ns([&] { M m; for (std::size_t i = 0; i < sorted.size(); ++i) m.emplace(sorted[i], int(i)); sink = m.size(); }, n);
	double sub = best_ns([&] { M m; for (std::size_t i = 0; i < keys.size(); ++i) m[keys[i]] = int(i); sink = m.size(); }, n);
	M m;
	for (std::size_t i = 0; i < keys.size(); ++i) m.emplace(keys[i], int(i));
	double find = best_ns([&] { std::uint64_t s = 0; for (auto& k : keys) s += m.find(k)->second; sink = s; }, n);
	std::printf("  %-14s emplace random %6.1f  sorted %6.1f  operator[] %6.1f  find %6.1f ns\n", name, rnd, srt, sub, find);
}

int main() {
	for (std::size_t n : {20000u, 200000u}) {
		std::mt19937_64 rng(9);
		std::vector<std::string> keys(n);
		for (auto& k : keys) k = "key/" + std::to_string(rng()) + "/item";
		auto sorted = keys;
		std::sort(sorted.begin(), sorted.end());
		std::printf("n=%zu\n", n);
		row<btree::map<std::string, int>>("btree::map", keys, sorted);
		row<absl::btree_map<std::string, int>>("absl::btree_map", keys, sorted);
	}
}
