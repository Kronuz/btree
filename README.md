# btree

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/btree/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/btree/actions/workflows/ci.yml)
[![License: Apache 2.0](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

B-tree containers for C++17: the `std::map` and `std::set` interfaces with less memory and fewer
cache misses. Each 256-byte node holds many values and leaves carry no child pointers, so a
lookup compares several keys per cache line it loads and a `set<int32_t>` spends about one byte
of structure per value. Based on [Google's C++ B-tree](https://code.google.com/archive/p/cpp-btree/), with
the interface of the C++17 standard containers. Reach for it for large ordered maps and sets
that are not holding iterators across mutations.

## ✨ Highlights

- **Less memory.** A `btree::set<std::int32_t>` filled in random order takes about 5.2 bytes
  per value (`bytes_used()`), one byte over the value itself; a red-black tree node carries
  three pointers and a color for every value.
- **Four containers.** `btree::map`, `set`, `multimap` and `multiset`, with `emplace`,
  `try_emplace`, heterogeneous lookup through a transparent comparator, and values that need
  no default constructor.
- **Keys move.** Splits, merges and erases move keys instead of copying them; with
  nothrow-movable keys an insertion allocates only its new nodes and the value, an erase
  allocates nothing, and maps accept move-only keys.
- **Failure-atomic for allocation.** With nothrow-movable keys and values that fit a node, a
  refused allocation or a throwing constructor leaves the contents, the structure and the
  live allocations unchanged.
- **Cheap moves.** A move hands over the root: node allocations and element addresses
  transfer, and the source stays empty and reusable.
- **No dependencies.** Header-only, standard library only.

## 🚀 Quick start

```cpp
#include "btree/map.h"

#include <cstdio>
#include <string>

int main() {
	btree::map<int, std::string> m;
	m.emplace(2, "two");
	m.try_emplace(1, "one");
	m[3] = "three";
	for (const auto& [key, value] : m) {   // ordered, like std::map
		std::printf("%d=%s ", key, value.c_str());
	}
	std::printf("\n");   // 1=one 2=two 3=three
}
```

## 📦 Installation

Requires a C++17 compiler (C++20 also builds) and CMake 3.20 or newer, and nothing else. CI
builds it on Linux (x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan and UBSan
builds with object-size and pointer-overflow checks; it has no TSan job, because the containers
offer no concurrent mutation.

```cmake
include(FetchContent)
FetchContent_Declare(btree
  GIT_REPOSITORY https://github.com/Kronuz/btree.git
  GIT_TAG        c642876701bfab8af52c4399418aedd35b64fa00)  # master tracks the tip
FetchContent_MakeAvailable(btree)

target_link_libraries(my_app PRIVATE btree::btree)
```

Then `#include "btree/map.h"` or `#include "btree/set.h"`, or copy the `btree/` directory onto
your include path. `cpp_btree::cpp_btree` is an alias of the same target.

## 🎯 When to use it

It fits large ordered maps and sets where memory or lookup speed matters, including maps with
heap-allocated string keys.

Not for:

- **Iterators or references held across mutations.** Any insert or erase invalidates every
  iterator into the container; use `std::map` when they must survive.
- **Throwing moves under allocation failure.** Keys or values whose move may throw, and pairs
  larger than a node, get narrower failure guarantees ([docs/API.md](docs/API.md)).
- **Standard libraries whose two pair types differ in layout.** A map slot reads a
  `std::pair<Key, T>` through `std::pair<const Key, T>`; static assertions stop the build
  where their size, alignment or member offsets differ.
- **Nested containers with non-default-constructible allocators under Clang and libstdc++ 13
  in C++20.** Constructor-trait instantiation can fail there.

## ⚡ Performance

`string_insert_bench` times `emplace` (200,000 random string keys of 24 to 28 bytes,
`"key/<u64>/item"`, into an empty map) and `find` (a lookup of each of those keys) in ns per
operation, best of 5 per run, then the fastest of eight runs on the M4 Pro and three on the i9.
The columns are `btree::map`, `absl::btree_map` from
[abseil](https://github.com/abseil/abseil-cpp) 20260817.0 (also a B-tree) and `std::map` from
libc++ 20.1 (a red-black tree). The benchmark checks that the three maps hold the same keys and
return the same value for every key, and stops otherwise. Apple clang 17.0.0 with libc++, C++20,
`-O3 -DNDEBUG`, on an Apple M4 Pro (macOS 26.7) and an Intel Core i9-9980HK (macOS 15.8):

| Operation | `btree::map`, M4 Pro | `absl::btree_map`, M4 Pro | `std::map`, M4 Pro | `btree::map`, i9-9980HK | `absl::btree_map`, i9-9980HK | `std::map`, i9-9980HK |
|---|--:|--:|--:|--:|--:|--:|
| `emplace` | 302 | 271 | 379 | 697 | 628 | 757 |
| `find` | 283 | 306 | 319 | 630 | 556 | 631 |

Run by run, `btree::map`'s time against `absl::btree_map`'s is 16% less to 24% more for `emplace`
and 9% less to 14% more for `find`; against `std::map`'s it is 6% to 39% less for `emplace` and
17% less to 0% more for `find`. The i9's runs differ by up to 3% from one another and the M4 Pro's
by up to 37%, as other work shares that machine.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target string_insert_bench
./build/string_insert_bench
```

The benchmark needs abseil. [docs/perf/](docs/perf/) lists every harness and has the
key-relocation measurements, heap-call counts included.

## 📚 Documentation

- [docs/API.md](docs/API.md): iterator invalidation, failure guarantees, ownership moves and
  comparators.
- [docs/NODE_LAYOUT.md](docs/NODE_LAYOUT.md): compact node storage, slot lifetimes,
  alignment, allocator compatibility and the mutation paths.
- [docs/TESTING.md](docs/TESTING.md): the test scripts, sanitizers and the recorded
  qualification.
- [docs/perf/](docs/perf/): the benchmarks and their results.
- [CHANGELOG.md](CHANGELOG.md): changes with their measurements, newest first.
- [AGENTS.md](AGENTS.md): working notes for changing the library.
- [Catalog entry](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#btree) in Kronuz
  C++ Libraries.

## 🛠️ Building and testing

```sh
cmake -B build && cmake --build build && ctest --test-dir build
sh tests/run_layout.sh
sh tests/run_mixed_mode.sh
CXX=clang++ BTREE_TEST_SANITIZE=1 sh tests/run_layout.sh
CXX=clang++ BTREE_TEST_SANITIZE=1 sh tests/run_mixed_mode.sh
```

`ctest` runs the layout, insertion, ownership-move and relocation tests under C++17 and C++20,
with and without `NDEBUG`, and links Release and Debug translation units together in both
orders. The two scripts run the same suites outside CMake; `BTREE_TEST_SANITIZE=1` adds ASan and
UBSan with object-size and pointer-overflow checks.

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) and the catalog's
[rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
first; CI must pass on every platform.

## 📄 License

Apache 2.0, Copyright (c) 2019-2026 Germán Méndez Bravo and (c) 2013 Google Inc. See
[LICENSE](LICENSE) and [NOTICE](NOTICE). Based on
[Google's C++ B-tree](https://code.google.com/archive/p/cpp-btree/).
