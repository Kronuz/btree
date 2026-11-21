# Source layout

`btree/btree.h` is the include users name. It holds the library's license and caveats and the
standard includes, then includes the pieces below in dependency order. The pieces are internal:
include `btree/btree.h`, `btree/map.h` or `btree/set.h`, never a piece. Each piece opens `namespace
btree` and holds one responsibility.

| File | Holds |
|---|---|
| `btree/btree_compare.h` | swap helper, key-extraction tags, compare-to tag and adapters, key comparers |
| `btree/btree_params.h` | common, map and set parameter structs, map slot layout probe, node search dispatchers |
| `btree/btree_node.h` | `btree_node`: fixed-header node with trailing slots |
| `btree/btree_iterator.h` | `btree_iterator` |
| `btree/btree_tree.h` | the `btree` class: state, public operations, internal declarations |
| `btree/btree_node_impl.h` | out-of-class `btree_node` and `btree_iterator` methods |
| `btree/btree_tree_modify.h` | out-of-class `btree` modification: emplace, erase, rebalance, shrink |
| `btree/btree_tree_internal.h` | out-of-class `btree` internals: emplacement, locate and bounds, clear, dump, verify |
| `btree/btree_container.h` | `btree_container`, `btree_unique_container`, `btree_multi_container` |

`btree/map.h` and `btree/set.h` are unchanged.

## Tests

`tests/node_layout.cc` keeps the includes and `main`. Its pieces stay in one translation unit and one
anonymous namespace, so the `layout_*` targets and `tests/run_layout.sh` build as before.

| File | Holds |
|---|---|
| `tests/node_layout/fixtures.hh` | check counter, counting allocator |
| `tests/node_layout/compact_roots.hh` | compact roots, unpadded leaves, move-only and aligned slots, allocation refusal |
| `tests/node_layout/copy_failures.hh` | range insertion, copy and assignment failure positions |
| `tests/node_layout/root_growth.hh` | root growth, nonfull leaf and append failures, move-only recovery |
| `tests/node_layout/empty_insert.hh` | empty-tree insertion, comparator expressions, range insertions |
| `tests/node_layout/prepared.hh` | prepared-key failures, forwarding, ordered model |
