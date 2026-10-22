# cpp-btree

![C++17](https://img.shields.io/badge/C%2B%2B-17%20%7C%2020-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)
[![License: Apache 2.0](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)

`std::map` and `std::set`, but B-trees: in-memory ordered containers that use a
fraction of the memory and fewer cache misses. Based on
[Google's C++ B-tree](https://code.google.com/archive/p/cpp-btree/), modernized to
behave like the C++17 standard containers.

## ✨ Highlights

- 🧠 **Less memory**: about 1 byte of overhead per `int32_t` in `btree::set`, against
  16 bytes for a red-black tree node on a 32-bit system.
- ⚡ **Cache-friendly**: several key comparisons per node, so large containers
  search faster.
- 🔁 **Near drop-in**: `btree::map`, `set`, `multimap`, and `multiset` with
  `emplace`, `try_emplace`, heterogeneous lookup, and values that need no default
  constructor.
- ⚠️ **One difference**: any insert or erase invalidates every iterator into the
  container.

## 📦 Installation

Header-only and dependency-free: copy the `btree/` directory onto your include path.
It builds as C++17 or C++20.

## 🚀 Quick start

```cpp
#include "btree/map.h"

#include <cassert>
#include <string>

int main() {
	btree::map<int, std::string> m;
	m.emplace(2, "two");
	m.try_emplace(1, "one");
	assert(m.begin()->second == "one");   // ordered, like std::map
	assert(m.count(2) == 1);
}
```

## 🎯 When to use it

- ✅ Large ordered maps and sets where memory or lookup speed matters.
- ❌ Code that keeps iterators or references across inserts and erases: use
  `std::map`.

## 📚 Documentation

- [docs/TESTING.md](docs/TESTING.md): the mixed-mode regression script, sanitizers,
  and the recorded benchmark comparison.
- [docs/NODE_LAYOUT.md](docs/NODE_LAYOUT.md): compact node storage, slot lifetimes,
  alignment and allocator compatibility.

## 🛠️ Testing

```sh
sh tests/run_mixed_mode.sh                         # release/debug TU mixes, C++17 and C++20
CXX=clang++ BTREE_TEST_SANITIZE=1 sh tests/run_mixed_mode.sh   # with ASan and UBSan
sh tests/run_layout.sh                             # compact roots and counting allocators
CXX=clang++ BTREE_TEST_SANITIZE=1 sh tests/run_layout.sh
```

## 🤝 Contributing

Issues and pull requests are welcome.

## 📄 License

Apache 2.0. See [LICENSE](LICENSE). Copyright (c) 2013 Google Inc. and (c) 2019
Germán Méndez Bravo.

Range insertion and range construction refresh their end hint after each insertion, including root growth and splits. The compact-layout suite compares all four container families with standard ordered containers for duplicate input and populated destinations.

Comparator return-type checking uses const references and does not require a default-constructible key. Comparators still return `bool`, or `int` when using the compare-to tag.
