// std::string-keyed insert/find: btree::map against absl::btree_map and std::map. ns per
// operation, best of 5. Every map must hold the same keys and return the same value for every
// key; the run aborts otherwise.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
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
static std::uint64_t row(const char* name, const std::vector<std::string>& keys, const std::vector<std::string>& sorted) {
	const double n = double(keys.size());
	double rnd = best_ns([&] { M m; for (std::size_t i = 0; i < keys.size(); ++i) m.emplace(keys[i], int(i)); sink = m.size(); }, n);
	double srt = best_ns([&] { M m; for (std::size_t i = 0; i < sorted.size(); ++i) m.emplace(sorted[i], int(i)); sink = m.size(); }, n);
	double sub = best_ns([&] { M m; for (std::size_t i = 0; i < keys.size(); ++i) m[keys[i]] = int(i); sink = m.size(); }, n);
	M m;
	for (std::size_t i = 0; i < keys.size(); ++i) m.emplace(keys[i], int(i));
	double find = best_ns([&] { std::uint64_t s = 0; for (auto& k : keys) s += m.find(k)->second; sink = s; }, n);
	// The values are the key's index and the keys are distinct, so equal maps give equal checks.
	std::uint64_t check = m.size();
	for (auto& k : keys) check = check * 1099511628211ull + std::uint64_t(m.find(k)->second);
	std::printf("  %-18s emplace random %6.1f  sorted %6.1f  operator[] %6.1f  find %6.1f ns\n", name, rnd, srt, sub, find);
	return check;
}

int main() {
	for (std::size_t n : {20000u, 200000u}) {
		std::mt19937_64 rng(9);
		std::vector<std::string> keys(n);
		for (auto& k : keys) k = "key/" + std::to_string(rng()) + "/item";
		auto sorted = keys;
		std::sort(sorted.begin(), sorted.end());
		std::printf("n=%zu\n", n);
		auto a = row<btree::map<std::string, int>>("btree::map", keys, sorted);
		auto b = row<absl::btree_map<std::string, int>>("absl::btree_map", keys, sorted);
		auto c = row<std::map<std::string, int>>("std::map", keys, sorted);
		if (a != c || b != c) {
			std::fprintf(stderr, "the maps disagree\n");
			return EXIT_FAILURE;
		}
	}
}
