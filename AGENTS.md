# Working notes

This is a dependency-free, header-only C++17/C++20 ordered-container library. The implementation lives in `btree/btree.h`; `btree/map.h` and `btree/set.h` expose the containers. Read `docs/NODE_LAYOUT.md` before changing node storage.

Keep compact root allocations. Every allocation contains a complete fixed header and separately constructed trailing slots; full leaf/internal/root descriptor objects are never placed in short storage. Derive allocation, access, deallocation and byte statistics from the same layout helpers. Preserve value and pointer alignment, live value counts and matching allocator type/count pairs.

A map slot is a `slot_type` union whose live member is the mutable `std::pair<Key, T>`. Construct, relocate, swap and destroy through `mutable_value`; hand out `value`. Never `const_cast` a key: moving from or writing to a const object is undefined behavior, and an object created as `pair<const Key, T>` stays const. Keep every allocation of an insertion ahead of its first value move, so a refusal leaves the tree unchanged, and keep erase allocation-free.

Inactive child values remain unspecified in both Debug and Release. Changing diagnostics must not make mutation behavior depend on `NDEBUG`. Inserts and erases invalidate iterators as documented; do not promise STL node-address stability.

Run `sh tests/run_layout.sh` and `sh tests/run_mixed_mode.sh`, then both with `CXX=clang++ BTREE_TEST_SANITIZE=1`. Tests must exercise C++17/C++20 and Release/Debug with runtime checks active. On Linux with GCC 11 or Clang 12, run the sanitized scripts under `setarch $(uname -m) -R`. Fixtures that replace global `operator new` keep their counters `volatile`, because GCC 16 assumes those operators touch no global state. Run downstream integration and before/after `benchmarks/mutation_bench.cc` under the workspace quiet protocol. The older mixed-mode baseline does not qualify later layout changes.

Record behavior changes and measurements in `CHANGELOG.md`; the README and `docs/` describe the current library. Use `.scratch/` for artifacts. Preserve Apache 2.0 source notices and public container APIs. Header revisions require a complete downstream rebuild; the mixed-mode guarantee applies to one revision built with different diagnostic flags.
