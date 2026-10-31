// Map relocation moves keys and never copies them. Splitting, rebalancing,
// merging and root growth relocate std::pair<Key, T> slots; a key type that
// counts its copies proves that none happen, and a random operation stream
// with heap-allocated std::string keys is compared against std::map.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>

#include "btree/map.h"

#define CHECK(cond)                                                              \
	do {                                                                         \
		if (!(cond)) {                                                           \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			std::abort();                                                        \
		}                                                                        \
	} while (0)

static long copies = 0;

struct Key {
	std::string s;
	explicit Key(std::string v) : s(std::move(v)) {}
	Key(const Key& o) : s(o.s) { ++copies; }
	Key(Key&& o) noexcept : s(std::move(o.s)) {}
	Key& operator=(const Key& o) { ++copies; s = o.s; return *this; }
	Key& operator=(Key&& o) noexcept { s = std::move(o.s); return *this; }
	bool operator<(const Key& o) const { return s < o.s; }
};

int main() {
	// Contents against std::map, with keys longer than any small-string buffer.
	for (unsigned seed = 1; seed <= 3; ++seed) {
		auto mk = [](std::uint64_t n) { return std::string("some/long/key/prefix/that/is/not/sso/") + std::to_string(n); };
		std::mt19937_64 rng(seed);
		btree::map<std::string, int> m;
		std::map<std::string, int> ref;
		for (int i = 0; i < 20000; ++i) {
			auto k = mk(rng() % 3000);
			switch (rng() % 4) {
				case 0: case 1: CHECK(m.emplace(k, i).second == ref.emplace(k, i).second); break;
				case 2: CHECK(m.erase(k) == ref.erase(k)); break;
				case 3: { auto a = m.find(k); auto b = ref.find(k);
					CHECK((a == m.end()) == (b == ref.end())); if (b != ref.end()) CHECK(a->second == b->second); break; }
			}
		}
		CHECK(m.size() == ref.size());
		m.verify();
		auto it = m.begin();
		for (auto& kv : ref) { CHECK(it->first == kv.first && it->second == kv.second); ++it; }
	}

	// Keys are moved in, and building, rebalancing and erasing copy none of them.
	for (unsigned seed = 1; seed <= 3; ++seed) {
		std::mt19937_64 rng(seed);
		btree::map<Key, int> m;
		copies = 0;
		for (int i = 0; i < 20000; ++i) m.emplace(Key("k" + std::to_string(rng() % 100000)), i);
		const long build = copies;
		for (int i = 0; i < 20000; ++i) m.erase(Key("k" + std::to_string(rng() % 100000)));
		m.verify();
		std::fprintf(stderr, "seed %u: key copies while building: %ld, erasing: %ld\n", seed, build, copies - build);
		CHECK(build == 0);
		CHECK(copies == build);
	}
	std::puts("btree key relocation tests passed");
}
