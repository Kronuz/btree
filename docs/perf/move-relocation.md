# Move relocation: measurements

Measured 2026-10-31 on working-tree candidates of the series that landed as `btree:d81f2a3` through `btree:512febb`.

## Change

A map slot is a union whose live member is a mutable `std::pair<Key, T>`, read through its `std::pair<const Key, T>` member (the layout-checked technique of Abseil's btree). Splits, rebalances, merges, erases and root replacement move keys; the old slot type copied them. Every allocation of an insertion happens before the first value move, and erase allocates nothing. [NODE_LAYOUT.md](../NODE_LAYOUT.md) describes the mechanism and [CHANGELOG.md](../../CHANGELOG.md) lists the commits.

Maps whose keys allocate (`std::string` beyond the small-string buffer) should get faster, and integer keys should stay where they were. The rows below check both.

## Method

Arms, one binary each per compiler:

- `master`: `25f9494`, slots of `std::pair<const Key, T>` that copy keys on relocation.
- `main`: an earlier, parallel fix at `a0b60e3` that moved keys through `const_cast`. It modified const objects and did not land. It is the speed reference for string keys.
- `now`: the candidate.
- `now (A/A)`: a byte-identical copy of `now`. The gap between the two is the noise floor of a batch.

Each batch alternates the arms and reverses their order every round, under a hold that keeps other work off the machine. A row is the best of 5 to 9 repetitions within a run.

Machines and compilers:

- Intel Core i9-9980HK, macOS 15 x86_64: Apple clang 17 (libc++) and GCC 16.2 (libstdc++), `-std=c++20 -O3 -DNDEBUG`. Every timing table.
- Apple M4 Pro: Apple clang 17, one string-key batch.
- Linux x86_64 (kernel 5.15), GCC 11.2 and Clang 12.0.1 with libstdc++ 11: layout and sanitizer matrix only, never timing.

Harnesses:

- `benchmarks/string_insert_bench.cc`: string keys against `absl::btree_map` (it needs abseil).
- `benchmarks/mutation_bench.cc --keys=N`: 65,536 random insertions then erasures, 80-byte mapped strings, process CPU time.
- `benchmarks/relocation_alloc_bench.cc`: heap calls and requested bytes of the same workload.
- The integer rows come from a small timing harness that is not in the repository: `btree::map<uint64_t, uint64_t>` insert, find (shuffled keys), iterate, and copy followed by erase, at n = 1,000, 100,000 and 1,000,000, nanoseconds per operation.

## Results

### String keys, n = 200,000, i9, ns per operation

Keys are `"key/<u64>/item"`, 24 to 28 bytes. The `master` column is the median of a 5-round batch; the others are medians of an 11-round batch.

| Row | master | main | now | `absl::btree_map` |
|---|---|---|---|---|
| `emplace`, Apple clang 17 | 1658 | 677 | 679 | 618-625 |
| `emplace`, GCC 16 | 1288 | 646 | 659 | |
| `find`, Apple clang 17 | 613 | 635 | 643 | 572 |
| `find`, GCC 16 | 609 | 585 | 581 | |

`now` is 2.4x (Clang) and 2.0x (GCC) faster than `master` at `emplace`. In the 5-round batch, copy followed by erase falls from 833 to 696 ns (Clang) and from 832 to 662 ns (GCC).

### `now` against `main`, string `emplace`

Paired medians: the ratio of `now` to `main` within each round, then the median over the rounds of a batch. A positive number means `now` is slower. Single rounds swing by up to 8% on this host, and the two A/A copies of `now` differ by -1.6% to +1.8% in the batches that carry them.

| Batch | Clang | GCC |
|---|---|---|
| 5 rounds, with the A/A arm | +0.9% | +1.4% |
| 9 rounds, with the prototype arm | -0.6% | +2.5% |
| 9 rounds, with the shift variant | +1.8% | +4.7% |
| 11 rounds, with the hybrids | +1.0% | +1.2% |

`now` is 0% to 2% slower than `main` with Clang and 1% to 5% slower with GCC.

### Variants that do not explain the gap

Paired medians against `main`, in the 11-round and 9-round batches:

| Variant | Clang | GCC |
|---|---|---|
| `now` | +1.0% | +1.2% |
| `now` with `master`'s `rebalance_or_split` | +1.0% | +0.1% |
| `now` building the incoming value after the split | +1.1% | +1.3% |
| `now` shifting by move in full-leaf insertion | +0.3% | +2.2% |
| copy of the shift variant (A/A) | +1.0% | +3.3% |

The first three land within 1.3% of `main`, as `now` does in the same batch. The shift variant sits inside its own A/A spread. None accounts for the difference.

### Integer keys, i9, ns per operation

`now` against `master`, medians of a 5-round batch. The largest differences are +3.5% (GCC iterate at 1,000,000, whose A/A copies differ by 6%) and -3.4% (Clang copy and erase at 100,000). Every row sits inside the batch's A/A spread.

| Row (n) | Clang master | Clang now | GCC master | GCC now |
|---|---|---|---|---|
| insert (1,000) | 71.1 | 73.1 | 70.0 | 70.0 |
| insert (100,000) | 113.9 | 113.3 | 110.9 | 113.4 |
| insert (1,000,000) | 187.2 | 189.6 | 189.0 | 185.2 |
| find (1,000) | 36.1 | 36.2 | 42.0 | 42.0 |
| find (100,000) | 79.9 | 77.5 | 82.2 | 83.7 |
| find (1,000,000) | 173.0 | 172.3 | 183.7 | 180.6 |
| iterate (1,000) | 1.2 | 1.2 | 1.0 | 1.0 |
| iterate (100,000) | 1.9 | 1.9 | 1.8 | 1.8 |
| iterate (1,000,000) | 6.6 | 6.7 | 6.6 | 6.4 |
| copy and erase (1,000) | 60.0 | 61.7 | 65.0 | 65.0 |
| copy and erase (100,000) | 102.7 | 101.4 | 103.6 | 105.6 |
| copy and erase (1,000,000) | 186.7 | 189.2 | 197.1 | 197.8 |

### Mutation benchmark, i9, process CPU ms

`master` against `now`, medians of a 5-round batch. Keys of 16 bytes fit libc++'s small-string buffer and exceed libstdc++'s.

| Keys | Clang Release | GCC Release | Clang Debug | GCC Debug |
|---|---|---|---|---|
| 12 bytes | 281 -> 267 (0.95) | 296 -> 300 (1.01) | 274 -> 285 (1.04) | 310 -> 329 (1.06) |
| 16 bytes | 265 -> 256 (0.97) | 578 -> 403 (0.70) | | |
| 32 bytes | 689 -> 350 (0.51) | 642 -> 373 (0.58) | | |
| 200 bytes | 799 -> 546 (0.68) | 737 -> 539 (0.73) | 817 -> 556 (0.68) | 745 -> 552 (0.74) |

The 12-byte Debug rows come from one 5-round batch without an A/A arm. A 9-round batch with one puts `now` within 1% of `main` there, with A/A spreads of 5% to 10%.

### Heap calls, 65,536 entries, 80-byte mapped strings

`master` against `now`. `main` differs from `now` by at most 14 calls.

| Library, key bytes | Insert calls | Insert bytes | Erase calls |
|---|---|---|---|
| libc++, 12 or 16 | 82,130 -> 82,123 | 10,173,672 -> 10,173,056 | 0 -> 0 |
| libc++, 200 | 420,230 -> 147,659 | 80,498,472 -> 23,804,544 | 72,692 -> 0 |
| libstdc++, 16 | 347,640 -> 159,384 | 15,785,232 -> 12,584,688 | 66,768 -> 0 |
| libstdc++, 200 | 347,640 -> 159,384 | 62,482,408 -> 24,643,312 | 66,768 -> 0 |

### Apple M4 Pro

`string_insert_bench`, 7 rounds, Apple clang 17. At n = 20,000, `emplace` takes 183 ns with `main`, 188 ns with `now` and 176 to 180 ns with `absl::btree_map`. The n = 200,000 rows on that shared host had A/A spreads above 10% and carry no conclusion.

## Proof

- Oracle: `std::map` with the same operations, plus the contract that a refused insertion leaves contents, structure and live allocations unchanged.
- `tests/relocation_failure.cc` refuses every allocation point of every insertion and erases with the heap closed. Against `master` it fails (live allocations change after a refused insertion).
- `tests/key_relocation.cc` requires zero key copies. Against `master` it reports 107,280 copies while building (seed 1).
- Revert checks: without the post-split repair `internal_verify` finds an empty node, and with the size counted first the size check fails. `node_layout` fails with the prototype's root-growth rule (keys cleared).
- Linux x86_64, GCC 11.2 and Clang 12.0.1: `tests/run_layout.sh` (28 cases), `tests/run_mixed_mode.sh` (16 cases) and `key_relocation` (C++17 and C++20) pass natively and under ASan, UBSan, object-size and pointer-overflow checks, run under `setarch -R`. Clang 12 links ASan's runtime statically and its `operator delete` clashes with the tests' replacements, so the sanitized Clang runs use `-shared-libsan`. The sanitized `prepared_commit` also passes with address randomization on, with both compilers.
- macOS x86_64, GCC 16.2, Apple clang 17 and LLVM 23: both scripts and `key_relocation` pass natively.
- GCC 16 at `-O2` and `-O3` with `-std=c++20` assumes replaceable `operator new` and `operator delete` touch no global state (`-fassume-sane-operators-new-delete`, on by default), and libstdc++ allocates through `__builtin_operator_new`. A test that counts allocations in plain globals then reads stale counters: the previous `prepared_commit` and `relocation_failure` aborted on their final `live_allocations == initial` check with `master`'s header and with this one. `-fno-assume-sane-operators-new-delete` removes the abort with either header. The counters are `volatile` (`btree:7aec4ce`).

## Decision and open items

- The live-union slot lands. Integer rows are flat inside the noise, string `emplace` is 2.0x to 2.4x faster, and heap calls for 200-byte keys fall by 65% (libc++) and 54% (libstdc++).
- GCC `emplace` with string keys is 1% to 5% slower than the `const_cast` variant, and Clang 0% to 2%. The variants above do not localize it. Counter-level evidence (instructions, branch misses) has not been collected.
- The M4 Pro rows at n = 200,000 need a quieter host before they carry a number.
