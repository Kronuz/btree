# btree

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/btree/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/btree/actions/workflows/ci.yml)

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
It builds as C++17 or C++20. CMake consumers can use `add_subdirectory` or FetchContent and link `btree::btree`; tests build only when this repository is the top-level project.

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

## ⚡ String keys

A `btree::map` slot stores a mutable `std::pair<Key, T>` and hands it out as
`std::pair<const Key, T>&`, so splitting, rebalancing, merging and erasing move
keys instead of copying them. With nothrow-movable keys and mapped values, such
as `std::string`, an insertion allocates only its new nodes and the incoming
value, and an erase allocates nothing. Maps accept move-only keys.
[docs/NODE_LAYOUT.md](docs/NODE_LAYOUT.md) explains the mechanism.

## 📊 Performance

On an Intel i9-9980HK (macOS, Apple clang 17, `-O3`), with 200,000 random
string keys of 24 to 28 bytes, `emplace` takes 679 ns and `find` 643 ns per
operation; `absl::btree_map` takes about 620 and 572 ns. The benchmark needs
abseil:

```sh
cmake -B build && cmake --build build --target string_insert_bench
./build/string_insert_bench
```

## 📚 Documentation

- [docs/TESTING.md](docs/TESTING.md): the mixed-mode regression script, sanitizers,
  and the recorded qualification.
- [docs/NODE_LAYOUT.md](docs/NODE_LAYOUT.md): compact node storage, slot lifetimes,
  alignment, allocator compatibility, and the mutation paths.
- [CHANGELOG.md](CHANGELOG.md): what changed, with the before/after measurements.

## 🛠️ Testing

```sh
sh tests/run_mixed_mode.sh                         # release/debug TU mixes, C++17 and C++20
CXX=clang++ BTREE_TEST_SANITIZE=1 sh tests/run_mixed_mode.sh   # with ASan and UBSan
sh tests/run_layout.sh                             # compact roots and counting allocators
CXX=clang++ BTREE_TEST_SANITIZE=1 sh tests/run_layout.sh
```

## 🧱 Failure guarantees

When the key and mapped value move without throwing (`std::string`, integers,
variants and nested `btree::map`s of such types) and the pair fits the target
node size (256 bytes by default), insertion and erase are failure-atomic for
allocation:

- **Insertion** constructs the incoming value first, then allocates every node
  a split needs, from the leaf up to a new root, before it moves any value. A
  refused allocation or a throwing value constructor leaves the contents, the
  structure and the live allocations unchanged.
- **Erase**, merging and root collapse allocate nothing.

Pairs larger than the target node size, and types whose move may throw, have
narrower guarantees. A throw from a move in the middle of a full-node split,
rebalance or merge is not recovered.

- **Full-leaf insertion** splits first and then constructs the value in its
  slot. If the construction fails, the leaf the split left short is merged or
  rebalanced as erase does: the contents, the size and the structure stay
  valid, and the split may remain.
- **Nonfull middle insertion** and **compact leaf-root growth** construct the
  incoming value first in its final replacement slot, then relocate existing
  values under counted ownership. Failed copying leaves the original contents
  intact; a move-only mapped value whose move throws can remain moved-from,
  with every key in place. If a destructive move of a noncopyable key fails,
  the container becomes empty and reusable. Explicit moves from contained
  mapped arguments may consume them.

For every type:

- **First insertion** publishes its root only after constructing the value. An
  allocation or value-construction failure leaves a reusable empty container.
- **Nonfull leaf insertion** publishes the internal-root size after the value is
  constructed. A refused append leaves the previous size intact, and a retry
  reuses the existing capacity.
- **Copies** build a compact ordered shape without key comparisons and release
  every completed value and node on failure, including nested containers. Copy
  assignment clears, then copies: a failed copy leaves the destination empty.
- **Range insertion and range construction** refresh their end hint after each
  insertion, including root growth and splits. The compact-layout suite compares
  all four container families with standard ordered containers for duplicate
  input and populated destinations.

Comparator return-type checking uses const references and does not require a
default-constructible key. Comparators return `bool`, or `int` with the
compare-to tag.

## 🔀 Ownership moves

All four containers transfer their root when constructed or assigned from a
non-const rvalue. The source becomes empty and reusable; node allocations and
element addresses transfer unchanged. Construction copies comparator/allocator
state before taking ownership. Assignment first clears the destination with its
previous allocator, then copies source state and takes its root. Assignment
follows the library's source-allocator adoption convention, including for
unequal stateful allocators.

If state preparation throws, the source keeps its nodes. A failed move
assignment leaves the destination empty. Self-move preserves the container;
const rvalues copy. Conditional `noexcept` follows actual comparator and
internal-allocator copy operations, independently of element movement.
Populated-destination assignment includes destruction work and is not
constant-time. Copy and member swap do not use this path. Rebuild every
consumer against the same header revision.

`tests/ownership_move.cc` checks exact identities and allocation ownership
through all four families, over-alignment, source reuse, and
comparator/allocator failures. `benchmarks/ownership_bench.cc` measures
construction, assignment into empty destinations, and assignment into populated
destinations separately; prepare all sources and observe resulting contents
outside the timed phase. Its allocator meters node requests, excluding string
heaps and RSS. The
[actual-model report](https://github.com/Kronuz/Detent/blob/main/benchmarks/btree-ownership-moves-20261022.json)
includes string heaps and states the remaining qualification gates.

## 📏 Benchmarks

- `benchmarks/mutation_bench.cc` times 65,536 random insertions and erasures
  of string keys; `--keys=N` pads keys to N bytes, and `--copy`, `--copy-pruned`
  and `--move-nested` time copies and nested moves. Set
  `-DBTREE_BENCH_CYCLES=64` for longer runs.
- `benchmarks/relocation_alloc_bench.cc` counts global `operator new` calls and
  requested bytes for the same insertions and erasures, for a given key length.
- `benchmarks/mutation_allocation_bench.cc` meters node requests, excluding
  string heaps.
- `benchmarks/string_insert_bench.cc` compares string-key `emplace`,
  `operator[]` and `find` with `absl::btree_map`.

Compare identical compiler flags and sources under an isolated measurement
window. [CHANGELOG.md](CHANGELOG.md) links the Detent reports that include
string heaps and retained memory for whole application models.

## ⚠️ Known limitations

- **Layout-checked slots.** A map slot is a union of `std::pair<Key, T>` and
  `std::pair<const Key, T>`, read through the const-key member while the
  mutable member is live, as in Abseil's btree. The standard leaves this
  layout implementation-defined; static assertions on size, alignment and
  member offsets stop the build on a library where the two pairs differ.
- **Nested containers with non-default-constructible allocators** can fail
  constructor-trait instantiation under Clang with libstdc++ 13 in C++20. The
  optional nested allocator benchmark hits the same limitation on some
  compiler/library combinations.

## 🔗 Repository name

`Kronuz/cpp-btree` is this repository's former name. GitHub redirects its
repository and clone URLs; the history and stars stay here. Keep the former name
unused to preserve those redirects. Header paths are `btree/map.h` and
`btree/set.h`. The `cpp_btree::cpp_btree` CMake target is an alias of the same
interface target as `btree::btree`.

## 🤝 Contributing

Issues and pull requests are welcome.

## 📄 License

Apache 2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE). Copyright (c) 2013 Google Inc. and (c) 2019-2026 Germán Méndez Bravo.
