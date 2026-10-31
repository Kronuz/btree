# Testing btree

Run `sh tests/run_mixed_mode.sh` to check release/debug translation-unit combinations in both link orders under C++17 and C++20. Set `CXX=clang++ BTREE_TEST_SANITIZE=1` for ASan/UBSan. The regression exercises insertion, split, erase, merge, clear, verification and destruction with tracked nontrivial values. Inactive value storage and child slots are unspecified; diagnostics check active entries and child links without requiring debug-only zeroing. This removes coupling through diagnostic slot state, rather than promising compatibility for arbitrary compiler, allocator or standard-library configurations.

The [recorded comparison](../benchmarks/mixed-mode-baseline.json) contains 16 native and 16 ASan/UBSan Linux regression cases and three local before/after benchmark pairs. Compile `benchmarks/mutation_bench.cc` with `-std=c++20 -O3`, the chosen include tree, and either `-DNDEBUG` or `-UNDEBUG`. The Release benchmark's disassembled text is identical before and after; timing variation is not attributed to this correction. No performance improvement is claimed.

Run `sh tests/run_layout.sh` for the compact node layout regression under C++17 and C++20, with checks active in Release and Debug. Set `CXX=clang++ BTREE_TEST_SANITIZE=1` for AddressSanitizer, UndefinedBehaviorSanitizer, object-size and pointer-overflow checks with recovery disabled. `tests/relocation_failure.cc` refuses every allocation point of insertion with keys longer than the small-string buffer (ascending, descending and shuffled, through splits, rebalances and root growth), requires contents, structure and live allocations to be unchanged after each refusal, and erases every entry with the heap closed (merges, rebalances and root collapse), requiring zero allocations. Build it with `-DRELOCATION_SKIP_MOVE_ONLY_KEYS` against a header that cannot compile move-only map keys. The layout script also checks exact allocator request/deallocation pairs, short-root capacities 1/2/4, split/merge/root collapse, unpadded value extents, move-only nontrivial mapped values, over-aligned slots, rejected header alignment, allocation failure and an ordered-container model.

`BTREE_TEST_BUILD` selects an isolated output directory. `BTREE_TEST_INCLUDE_ROOT` selects another revision's headers while preserving this regression source, and `BTREE_TEST_CASE=--short-root-only` selects the public-API short-root case. The separate `tests/short_root_repro.cc` uses only `map<string, int>` insertion, lookup, erasure and verification, so it also compiles against the original public revision. Compile that source with each include root and identical sanitizer flags to attribute the invalid compact-root access independently of the downstream application.

```sh
# Set include_root to the preserved original or corrected header tree.
mkdir -p .scratch/short-root
clang++ -std=c++20 -O1 -g -DNDEBUG -I "$include_root" \
    -fsanitize=address,undefined,object-size,pointer-overflow \
    -fno-sanitize-recover=all -fno-omit-frame-pointer \
    tests/short_root_repro.cc -o .scratch/short-root/repro
.scratch/short-root/repro
```

Compile `benchmarks/node_footprint.cc` with `-std=c++20 -O2 -DNDEBUG` against each include root. It records actual node requests, peak requested bytes and matching deallocations for short roots and 4,096-entry integer sets/string maps, separately from the container's reported byte count. The shared counting state itself and mapped string allocations are outside these node totals. Use the existing mutation benchmark for CPU comparisons without counter instrumentation.

The [compact layout record](../benchmarks/compact-layout-qualification.json) contains the October 18 manual qualification on macOS (Clang 23.1.2), Linux (Clang 18.1.3) and FreeBSD (Clang 19.1.7). Each platform passes four native and four sanitized layout cases, with 21,428 runtime checks per case, plus 16 native and 16 sanitized mixed-mode cases. The separate public-API probe fails against original `dbe7557` with UBSan's insufficient-space `leaf_fields` upcast diagnostic, and passes against the corrected header with identical flags. That manual record predates GitHub Actions integration; it is not an automated CI result.

The corrected header passes all three downstream Detent host ASan/UBSan cases in 46.12 seconds. Five uncontended alternating mutation pairs record median process CPU of 268.482 ms before and 269.630 ms after (+0.43%); paired changes range from -2.16% to +13.37%. This sample does not establish equivalence or a performance improvement. An earlier batch with competing compilation is excluded.

All 14 footprint rows preserve actual allocation counts, peak requested bytes and live node bytes. Empty containers now report zero node bytes, correcting 16 nonexistent bytes in the original report. The older mixed-mode comparison above does not qualify these layout changes.

## Automated qualification

The repository calls the shared cpp-libraries CMake workflow on pushes, pull requests, manual dispatch and nightly runs. Its matrix covers GCC and Clang on Linux x86_64, GCC on Linux ARM64, both macOS architectures, FreeBSD x86_64 and Linux ASan/UBSan with object-size and pointer-overflow checks. ThreadSanitizer is disabled because the containers expose no concurrent mutation API.

CMake registers 32 tests: four C++17/C++20 release/debug profiles each for layout, prepared insertion, ownership moves and relocation failure, plus sixteen mixed-mode combinations covering both object link orders. These build the same fixtures as the shell scripts. Global sanitizer and standard-library hardening flags reach all translation units. Every fixture uses runtime checks independent of `NDEBUG`. This matrix does not replace downstream publication-failure qualification or controlled performance comparisons.

```sh
cmake -S . -B .scratch/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build .scratch/cmake --parallel 4
ctest --test-dir .scratch/cmake --output-on-failure --parallel 2
```
