# Source layout

`btree/btree.h` is the include users name. It holds the library's license and caveats and the
standard includes, then includes the pieces below in dependency order. Each piece is a
self-contained header: it includes its siblings and the standard headers it uses and compiles
alone. Users still include `btree/btree.h`, `btree/map.h` or `btree/set.h`. Each piece opens
`namespace btree` and holds one class or one family of helpers, with that class's out-of-class
member definitions after it.

| File | Holds |
|---|---|
| `btree/btree_compare.h` | swap helper, key-extraction tags, compare-to tag and adapters, key comparers |
| `btree/btree_params.h` | common, map and set parameter structs, map slot layout probe, node search dispatchers |
| `btree/btree_node.h` | `btree_node`: fixed-header node with trailing slots, and its out-of-class member definitions (insert, remove, rebalance, split, merge, swap) |
| `btree/btree_iterator.h` | `btree_iterator`, and its out-of-class member definitions |
| `btree/btree_tree.h` | the `btree` class and all of its member definitions |
| `btree/btree_container.h` | `btree_container`, `btree_unique_container`, `btree_multi_container` |

`btree/map.h` and `btree/set.h` are unchanged.

## Kept whole

- `btree/btree_tree.h` (about 1,670 lines): the `btree` class declaration and every member
  definition. Insertion, erasure, rebalancing, shrinking, lookup and verification share the node
  and size invariants, so the members only make sense together; a cut between them would be a
  cut for a line count, not a unit someone opens alone.
- `tests/node_layout.cc` (1,087 lines): one subject, compact node layout regressions. Its checks
  share fixtures (counting allocator, throwing values) in one anonymous namespace and one `main`
  that sequences them, so cutting it would need linkage changes for no reader's benefit.
