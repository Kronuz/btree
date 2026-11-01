# btree benchmarks

The harnesses live in [`benchmarks/`](../../benchmarks/). Compare identical compiler flags and
sources on an otherwise idle machine, with an A/A arm to show the noise floor.

- `benchmarks/string_insert_bench.cc` compares string-key `emplace`, `operator[]` and `find`
  with `absl::btree_map`. CMake builds it (`--target string_insert_bench`) only when it finds
  abseil.
- `benchmarks/mutation_bench.cc` times 65,536 random insertions and erasures of string keys;
  `--keys=N` pads keys to N bytes, and `--copy`, `--copy-pruned` and `--move-nested` time
  copies and nested moves. Set `-DBTREE_BENCH_CYCLES=64` for longer runs.
- `benchmarks/relocation_alloc_bench.cc` counts global `operator new` calls and requested
  bytes for the same insertions and erasures, for a given key length.
- `benchmarks/mutation_allocation_bench.cc` meters node requests, excluding string heaps.
- `benchmarks/ownership_bench.cc` measures move construction and move assignment into empty
  and populated destinations.
- `benchmarks/node_footprint.cc` records node requests, peak requested bytes and matching
  deallocations for short roots and 4,096-entry sets and maps.

[move-relocation.md](move-relocation.md) has the method and results of the key-moving
relocation. [CHANGELOG.md](../../CHANGELOG.md) links the Detent reports that include string
heaps and retained memory for whole application models.
