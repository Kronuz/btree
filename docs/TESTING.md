# Testing btree

Run `sh tests/run_mixed_mode.sh` to check release/debug translation-unit combinations in both link orders under C++17 and C++20. Set `CXX=clang++ BTREE_TEST_SANITIZE=1` for ASan/UBSan. The regression exercises insertion, split, erase, merge, clear, verification and destruction with tracked nontrivial values. Inactive value storage and child slots are unspecified; diagnostics check active entries and child links without requiring debug-only zeroing, so Debug and Release translation units share no diagnostic slot state. This covers one header revision built with different diagnostic flags; it promises nothing for arbitrary compiler, allocator or standard-library configurations.

Compile `benchmarks/mutation_bench.cc` with `-std=c++20 -O3`, the chosen include tree, and either `-DNDEBUG` or `-UNDEBUG`.

Run `sh tests/run_layout.sh` for the compact node layout regression under C++17 and C++20, with checks active in Release and Debug. Set `CXX=clang++ BTREE_TEST_SANITIZE=1` for AddressSanitizer, UndefinedBehaviorSanitizer, object-size and pointer-overflow checks with recovery disabled. It checks exact allocator request/deallocation pairs, short-root capacities 1/2/4, split/merge/root collapse, unpadded value extents, move-only nontrivial mapped values, over-aligned slots, rejected header alignment, allocation failure and an ordered-container model.

The same script runs `tests/relocation_failure.cc`. It refuses every allocation point of every insertion, in ascending, descending and shuffled order, with keys longer than any small-string buffer, so the refusals land in splits, rebalances and root growth. After each refusal the contents, the structure and the live allocation count must be unchanged. Then it erases every entry, by key and through iterators, with the heap closed, and requires zero allocations. Values too large to build before a split, and values whose move may throw, are checked for consistent contents and size after each refusal. Build it with `-DRELOCATION_SKIP_MOVE_ONLY_KEYS` against a header that cannot hold move-only map keys.

`tests/key_relocation.cc` compares 20,000 random insertions, erasures and lookups with heap-allocated `std::string` keys against `std::map`, and requires zero key copies while building and erasing a map whose key type counts its copies.

`tests/prepared_commit.cc` and `tests/relocation_failure.cc` replace the global `operator new` and `operator delete` and count allocations in `volatile` globals. GCC 16 assumes by default that replaceable allocation functions read and write no global state (`-fassume-sane-operators-new-delete`); without `volatile` it reuses stale counter values across allocations at `-O2`.

On Linux with GCC 11 or Clang 12, run the sanitized scripts under `setarch $(uname -m) -R`. Clang 12 links ASan's runtime statically, and its `operator delete` clashes with these replacements at link time; pass a `CXX` wrapper that adds `-shared-libsan` and an rpath to the runtime directory.

`BTREE_TEST_BUILD` selects an isolated output directory. `BTREE_TEST_INCLUDE_ROOT` selects another revision's headers while preserving this regression source, and `BTREE_TEST_CASE=--short-root-only` selects the public-API short-root case. The separate `tests/short_root_repro.cc` uses only `map<string, int>` insertion, lookup, erasure and verification, so it compiles against any public revision. Compile that source with each include root and identical sanitizer flags to attribute the invalid compact-root access independently of the downstream application.

```sh
# Set include_root to the header tree under test.
mkdir -p .scratch/short-root
clang++ -std=c++20 -O1 -g -DNDEBUG -I "$include_root" \
    -fsanitize=address,undefined,object-size,pointer-overflow \
    -fno-sanitize-recover=all -fno-omit-frame-pointer \
    tests/short_root_repro.cc -o .scratch/short-root/repro
.scratch/short-root/repro
```

Compile `benchmarks/node_footprint.cc` with `-std=c++20 -O2 -DNDEBUG` against each include root. It records actual node requests, peak requested bytes and matching deallocations for short roots and 4,096-entry integer sets/string maps, separately from the container's reported byte count. The shared counting state itself and mapped string allocations are outside these node totals. Use the mutation benchmark for CPU comparisons without counter instrumentation.

## Recorded qualification

- [benchmarks/mixed-mode-baseline.json](../benchmarks/mixed-mode-baseline.json): 16 native and 16 ASan/UBSan Linux mixed-mode cases and three local benchmark pairs.
- [benchmarks/compact-layout-qualification.json](../benchmarks/compact-layout-qualification.json): the manual compact-layout qualification on macOS, Linux and FreeBSD.

[CHANGELOG.md](../CHANGELOG.md) summarizes both records, the downstream Detent runs and the Linux sanitizer matrix for each change.

## Automated qualification

The repository calls the shared cpp-libraries CMake workflow on pushes, pull requests, manual dispatch and nightly runs. Its matrix covers GCC and Clang on Linux x86_64, GCC on Linux ARM64, both macOS architectures, FreeBSD x86_64 and Linux ASan/UBSan with object-size and pointer-overflow checks. ThreadSanitizer is disabled because the containers expose no concurrent mutation API.

CMake registers 33 tests: four C++17/C++20 release/debug profiles each for layout, prepared insertion, ownership moves and relocation failure, sixteen mixed-mode combinations covering both object link orders, and a C++20 key-relocation test. These build the same fixtures as the shell scripts. Global sanitizer and standard-library hardening flags reach all translation units. Every fixture uses runtime checks independent of `NDEBUG`. This matrix does not replace downstream publication-failure qualification or controlled performance comparisons.

```sh
cmake -S . -B .scratch/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build .scratch/cmake --parallel 4
ctest --test-dir .scratch/cmake --output-on-failure --parallel 2
```
