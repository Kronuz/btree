# Changelog

All notable changes to `btree`. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the library has no
releases, so entries are dated.

## 2026-10-31

### Fixed

- **Map reshapes with heap-allocated keys under allocation failure.** Map
  slots held `std::pair<const Key, T>`, and moving such a pair copies its const
  key. With keys on the heap (`std::string` longer than the small-string
  buffer), every split, rebalance, merge, erase and root replacement allocated,
  and an allocation refused partway through a reshape lost or misordered
  entries, leaked, or double-freed on erase. Every revision since the 2019
  import behaved this way; Detent's allocation-failure sweep found it. Slots
  now store `std::pair<Key, T>` and relocation moves keys, every allocation of
  an insertion precedes its first value move, and erase allocates nothing
  (`d81f2a3`, `a56261d`). The oracle is `std::map` with the same operations,
  plus the contract that a refused insertion leaves contents, structure and
  live allocations unchanged.

  | Check | master `25f9494` | now |
  |---|---|---|
  | Detent allocation-refusal sweep (4 model tables x insert/erase/copy/destroy x short/long names) | long-name insert 33-65 violations, erase 3-4, crash (rc 134) | prototype `d81f2a3`: 0 violations in all 64 cells |
  | `tests/relocation_failure.cc` | fails: live allocations change after a refused insertion | passes |
  | `tests/key_relocation.cc` | fails: 107,280 key copies while building (seed 1) | passes: 0 copies |
  | Erasing 65,536 entries with 200-byte keys | 72,692 heap calls | 0 |

- **Failed construction after a split.** When the incoming value is built in
  its slot after the split (pairs larger than the target node size, or whose
  move may throw), a refused allocation or a throwing constructor left
  `size()` one above the number of entries and, when the split had emptied the
  target leaf (insertion at the front of a full leaf), an empty leaf linked in
  the tree whose destroyed slots iteration then read. The size counts the value
  only after it exists, and the empty or underfull leaf is merged or
  rebalanced as erase does (`512febb`).

### Changed

- A map slot is a `slot_type` union whose live member is the mutable pair,
  read through its `std::pair<const Key, T>` member, as in Abseil's btree
  (`a56261d`). Static assertions require both pair types to share size,
  alignment and member offsets. No `const_cast` remains; `params::swap` swaps
  the mutable pair. Node layout and sizes are unchanged. Apple clang 17 emits
  identical code for `benchmarks/mutation_bench.cc` with the live union and
  with the prototype's reinterpreted pair.
- Nonfull middle insertion shifts by move once the incoming value exists. It
  no longer prepares a copy of every shifted key in bounded scratch storage
  (`d81f2a3`).
- Compact-root growth of a map whose mapped type is move-only and may throw on
  move copies each key and moves the mapped value, so a failure keeps every key
  in place, as on master (`599e853`).

### Added

- Maps accept move-only keys.
- `tests/relocation_failure.cc` (refuses every allocation point of insertion,
  erases with the heap closed), `tests/key_relocation.cc` (zero key copies,
  contents against `std::map`), and a string-key variant of
  `root_growth_move_only_mapped`.
- `benchmarks/relocation_alloc_bench.cc`, `mutation_bench --keys=N`, and
  `benchmarks/string_insert_bench.cc` against `absl::btree_map`.

### Tests

- `tests/prepared_commit.cc` and `tests/relocation_failure.cc` keep their
  allocation counters `volatile` (`7aec4ce`). GCC 16 assumes replaceable
  `operator new` and `delete` touch no global state
  (`-fassume-sane-operators-new-delete`, on by default), and libstdc++
  allocates through `__builtin_operator_new`. At `-O2` and `-O3` with
  `-std=c++20`, `prepared_commit` aborted on its final `live_allocations ==
  initial` check, with master's header as with this one; C++17 and `-O1`
  passed, and `-fno-assume-sane-operators-new-delete` removed the abort with
  either header. The library was not at fault.

### Performance

Intel i9-9980HK, macOS 15 x86_64, `-std=c++20 -O3 -DNDEBUG`, each batch under a
`quiet` exclusive hold, arms alternating with the order reversed every round,
and a byte-identical copy of the new binary as an A/A arm. `master` is
`25f9494`; `main` is the deleted const_cast branch `a0b60e3`. Medians.

String keys (`"key/<u64>/item"`, 24 to 28 bytes), n = 200,000, ns per
operation, best of 7 per run. The master column comes from a 5-round batch,
the others from an 11-round batch:

| Row | master | `main` | now | `absl::btree_map` |
|---|---|---|---|---|
| `emplace`, Apple clang 17 (libc++) | 1658 | 677 | 679 | 618-625 |
| `emplace`, GCC 16 (libstdc++) | 1288 | 646 | 659 | |
| `find`, Apple clang 17 | 613 | 635 | 643 | 572 |

The new code is 2.4x (Clang) and 2.0x (GCC) faster than master at `emplace`.
Against `main` its paired medians over four batches of 5 to 11 rounds are 0-2%
slower with Clang and 1-5% slower with GCC, while the A/A arms spread 2-6%. Two
hybrids, the new code with master's `rebalance_or_split` and the new code
building the value after the split, landed within 1.3% of `main` like the new
code itself, so neither change accounts for the difference.

`benchmarks/mutation_bench.cc` (65,536 random insertions then erasures, 80-byte
mapped strings), process CPU ms, master against now:

| Keys | Clang Release | GCC Release | Clang Debug | GCC Debug |
|---|---|---|---|---|
| 12 bytes | 281 -> 267 (0.95) | 296 -> 300 (1.01) | 274 -> 285 (1.04) | 310 -> 329 (1.06) |
| 16 bytes | 265 -> 256 (0.97) | 578 -> 403 (0.70) | | |
| 32 bytes | 689 -> 350 (0.51) | 642 -> 373 (0.58) | | |
| 200 bytes | 799 -> 546 (0.68) | 737 -> 539 (0.73) | 817 -> 556 (0.68) | 745 -> 552 (0.74) |

Keys of 16 bytes fit libc++'s small-string buffer and exceed libstdc++'s. The
12-byte Debug rows come from one 5-round batch without an A/A arm; a 9-round
batch with one put the new code within 1% of `main` there, with A/A spreads of
5-10%.

The `u64` rows of the campaign benchmark (insert, find, iterate, copy and
erase at n = 1,000, 100,000 and 1,000,000) stay within 3.5% of master in both
compilers, inside their A/A spread.

Heap calls of `benchmarks/relocation_alloc_bench.cc` (65,536 entries, 80-byte
mapped strings), master against now; `main` differs from now by at most 14
calls:

| Library, key bytes | Insert calls | Insert bytes | Erase calls |
|---|---|---|---|
| libc++, 12 or 16 | 82,130 -> 82,123 | 10,173,672 -> 10,173,056 | 0 -> 0 |
| libc++, 200 | 420,230 -> 147,659 | 80,498,472 -> 23,804,544 | 72,692 -> 0 |
| libstdc++, 16 | 347,640 -> 159,384 | 15,785,232 -> 12,584,688 | 66,768 -> 0 |
| libstdc++, 200 | 347,640 -> 159,384 | 62,482,408 -> 24,643,312 | 66,768 -> 0 |

Apple M4 Pro, Apple clang 17, `string_insert_bench`, 7 rounds: at n = 20,000,
`emplace` takes 183 ns with `main`, 188 with the new code and 176-180 with
`absl::btree_map`. The n = 200,000 rows on that shared host had A/A spreads
above 10% and are not quoted.

### Qualification

- Linux x86_64 (kernel 5.15), GCC 11.2 and Clang 12.0.1 with libstdc++ 11:
  `tests/run_layout.sh` (layout, prepared insertion, ownership moves and
  relocation failure, each C++17/C++20 x Release/Debug), `tests/run_mixed_mode.sh`
  (16 cases) and `key_relocation` (C++17/C++20) pass natively and with
  ASan+UBSan+object-size+pointer-overflow, under `setarch -R`. Clang 12 links
  ASan's runtime statically, and its `operator delete` clashes with the tests'
  replacements at link time, so the sanitized Clang runs use `-shared-libsan`.
  The sanitized `prepared_commit` also passes with address randomization on,
  with both compilers; the GCC 11 AddressSanitizer spin seen on 2026-10-24 did
  not recur.
- macOS x86_64 (Intel i9-9980HK), GCC 16.2, Apple clang 17 and LLVM 23:
  both scripts and `key_relocation` pass natively, including
  `prepared_commit` at `-std=c++20 -O2` under GCC 16.
- GitHub Actions run 41249153852, on a tree identical to `512febb`: GCC and
  Clang on Linux x86_64, GCC on Linux arm64, macOS arm64 and x86_64, FreeBSD
  and Clang ASan/UBSan all pass.
- Revert checks, each against the code without its fix: `relocation_failure`
  fails against master (live allocations change after a refused insertion),
  without the post-split repair (`internal_verify`: an empty node) and with the
  size counted first (size check); `key_relocation` fails against master
  (107,280 key copies); `node_layout` fails with the prototype's root-growth
  rule (keys cleared); the previous `prepared_commit` and `relocation_failure`
  abort under GCC 16 `-O2 -std=c++20`.
- The Detent allocation-refusal sweep ran against the prototype `7130d24`
  (`d81f2a3` here), not against the later commits of this entry.

### Repository

The default branch is `master`. These branches were deleted after this entry
landed; their tips stay reachable by SHA:

- `move-relocation` `e3ff322b52ce0819969d19f52e6df5459e0b454b`: the prototype
  of this change, with its branch notes (`docs/MOVE_RELOCATION.md`,
  `docs/perf/move-relocation.md`), whose evidence this entry carries.
- `main` `a0b60e3d062d4586741a74b63fa4bc4f047f2ce2`: an earlier, parallel fix
  of the same copies that moved keys out of `pair<const Key, T>` through
  `const_cast`. Modifying a const object is undefined behavior, so it never
  landed; its changelog, string benchmark and key-copy test are folded in here.
- `heterogeneous-lookup` `f9c0ddaeaea907e744e05d78aa896af431ee736c`,
  `static-assert-compat` `367c5d61d8b936663acea9cc9c046d44806c7bc7`,
  `revert-destroy-value-assert` `7d88f0ca5dcaf446a2bbb0b25da338101f0266bb`,
  `construct-at-destroy-at` `e8615d13402e1ed6817e3dc64bc6d796c7473140`:
  pull-request branches already squash-merged as `ee30dc9`, `76e9aeb`,
  `d79133a` and `f90c2bf`.

## 2026-10-22

### Changed

- All four containers transfer their root on construction or assignment from a
  non-const rvalue, preparing comparator and allocator state first, so moved
  sources stay empty and reusable without touching their values. Copies and the
  legacy allocator adoption are preserved, with truthful conditional `noexcept`
  and explicit failure guarantees (`25f9494`).
- The pre-existing copy self-address check is corrected.
- Construction and populated-assignment costs are measured separately
  (`benchmarks/ownership_bench.cc`).

### Fixed

These corrections predate this repository's history and arrive squashed into
`25f9494`:

- The compact node layout: no allocation is treated as a complete
  `leaf_fields`, `internal_fields` or `root_fields` object. The public-API probe
  `tests/short_root_repro.cc` fails against the original `dbe7557` with UBSan's
  insufficient-space `leaf_fields` upcast diagnostic and passes against the
  corrected header with identical flags.
- `bytes_used()` reports zero node bytes for empty containers, correcting 16
  nonexistent bytes in the original report. All 14 footprint rows of
  `benchmarks/node_footprint.cc` preserve actual allocation counts, peak
  requested bytes and live node bytes.
- Diagnostics no longer require debug-only zeroing of inactive slots, which
  removes coupling between Debug and Release translation units through
  diagnostic slot state.
- Range insertion reacquires its end hint after each mutation.
- Comparator return-type checking uses const references and needs no
  default-constructible key.
- First insertion publishes its root only after constructing the value.
- Ordered copy construction releases every completed value and node on failure.
  Copy assignment keeps its clear-then-copy behavior.
- Compact leaf-root growth constructs the incoming value first and relocates
  under counted ownership.
- Nonfull leaf insertion publishes the internal-root size after a successful
  value construction.
- Nonfull middle insertion prepares fallible work before consuming originals.

### Qualification

- [benchmarks/mixed-mode-baseline.json](benchmarks/mixed-mode-baseline.json):
  16 native and 16 ASan/UBSan Linux regression cases and three local
  before/after benchmark pairs for the diagnostics change. The Release
  benchmark's disassembled text is identical before and after; timing variation
  is not attributed to that correction, and no performance improvement is
  claimed. This older baseline does not qualify the later layout changes.
- [benchmarks/compact-layout-qualification.json](benchmarks/compact-layout-qualification.json):
  the October 18 manual qualification on macOS (Clang 23.1.2), Linux
  (Clang 18.1.3) and FreeBSD (Clang 19.1.7). Each platform passes four native
  and four sanitized layout cases, with 21,428 runtime checks per case, plus 16
  native and 16 sanitized mixed-mode cases. The record predates the GitHub
  Actions integration; it is not an automated CI result.
- The corrected header passes all three downstream Detent host ASan/UBSan cases
  in 46.12 seconds. Five uncontended alternating mutation pairs record median
  process CPU of 268.482 ms before and 269.630 ms after (+0.43%); paired changes
  range from -2.16% to +13.37%. This sample does not establish equivalence or a
  performance improvement. An earlier batch with competing compilation is
  excluded.
- Detent reports:
  [prepared insertion](https://github.com/Kronuz/Detent/blob/main/benchmarks/btree-prepared-insertion-20261022.json)
  and [ownership moves](https://github.com/Kronuz/Detent/blob/main/benchmarks/btree-ownership-moves-20261022.json).

### Other

- The repository is renamed from `Kronuz/cpp-btree` to `Kronuz/btree` (the
  squashed history does not record the date). The
  `cpp_btree::cpp_btree` CMake target stays as an alias of `btree::btree`.
