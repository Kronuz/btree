# Compact node storage

Every node allocation contains a complete `btree_node` header. Values, internal child pointers and internal-root metadata occupy separately addressed trailing storage. No allocation is treated as a complete `leaf_fields`, `internal_fields` or `root_fields` object; those types remain size descriptors for source compatibility.

The header holds the existing leaf flag, position, capacity, live count and parent pointer. Value slots begin at the header size rounded to value alignment. Child slots follow the full value capacity, rounded to pointer alignment. Root metadata follows the internal slots. The offset helpers define allocation, access, deallocation and `bytes_used()` together.

Construction creates the header, starts each internal pointer slot's lifetime without initializing its value, and creates metadata only for internal roots. Each value slot begins its lifetime when an insertion or movement constructs a value there. Only live values are read or destroyed. Child pointers and root metadata are trivially destructible; their lifetimes end when the backing storage is released. Inactive child values remain unspecified, independently of `NDEBUG`.

## Footprint and alignment

For ordinarily aligned values, short-root requests retain the existing `sizeof(header) + capacity * sizeof(value)` size, with any required padding before values. Capacity still grows geometrically. Full leaves use that same extent, so allocation and deallocation agree even when a value size leaves tail padding in the former full-leaf struct. Internal storage adds only the alignment needed by child pointers and root metadata.

Ordinary node requests still rebind the supplied allocator to `char`. The returned block must align the node header and stored values; a misaligned block is returned to the allocator and rejected before construction. Values whose alignment exceeds `max_align_t` rebind the allocator to an aligned storage unit. Their request count and reported capacity include whole units, and deallocation uses the identical type and count.

The public container operations do not change. Node internals require a complete rebuild when changing header revisions; the mixed-mode tests cover Debug/Release combinations using the same revision. Custom allocators must support normal allocator rebinding, including aligned units when storing over-aligned values. Empty containers report only their object size in `bytes_used()`.

## Qualification

The counting-allocator test checks exact extents and matching deallocation, ordinary short-root capacities, unusually sized values, over-aligned values and allocation failure. Nontrivial values exercise construction, movement and destruction through splitting, merging and root collapse. Run it with sanitizer recovery disabled, then run the existing mixed-mode tests and downstream applications.

Preserve the old revision as the benchmark arm for allocation counts, peak requested bytes and mutation CPU. Compare identical keys, mapped values, insertion/erasure schedules and build flags. Treat an exact footprint change for formerly padded leaves or newly aligned storage separately from timing variation. No performance result follows from source inspection alone.

## Copy and compact-root failure ownership

Copy construction clones the existing topology without reinsertion. Each node publishes its live-value count after successful construction and tracks completed children independently. A failing clone destroys only constructed values and completed child subtrees before returning its allocation. Root parent and rightmost metadata are published after the complete clone succeeds.

Assignment clears its previous tree with the previous allocator, then copies comparator and allocator state before cloning. A failed clone leaves the destination empty; this is a basic exception guarantee, not preservation of its prior contents. The source remains unchanged. Comparator and allocator assignment retain their existing contracts.

Compact leaf growth stages replacement values with `move_if_noexcept`, cleans a failed replacement, and publishes the new root only after transfer succeeds. Copyable throwing values keep their original values during failed transfer. Move-only values whose move constructor can throw do not gain a strong value-preservation guarantee. A failed first value construction releases the empty leaf root, restoring the canonical null-root empty state before retry.

These guards do not establish general exception safety for arbitrary throwing split, rebalance, erase or const-key mutation. The positioned tests and downstream sanitizer qualification must distinguish those operations from the bounded copy and compact-growth change.

Topology cloning preserves the source's occupancy rather than compacting it through ordered reinsertion. Measure this separately: `mutation_bench --copy` and `--copy-pruned` report process CPU plus source/copy container bytes for populated and erase-thinned trees. Container bytes exclude heap storage owned by mapped strings and are not RSS. Compile the same benchmark source against both header revisions; the default mutation workload remains unchanged.

Logical root size advances after successful leaf insertion. A failed tail-value construction on a multilevel tree leaves its size unchanged. When promoting a full leaf root, allocate the new root and sibling before linking either; refusal of the sibling allocation frees the unpublished root. These bounded changes do not repair separator-transfer failures or internal-root promotion.

## Ownership moves

Ownership moves copy comparator and allocator state before stealing a node pointer, preserve element addresses, and leave sources empty and reusable. Move assignment first clears the destination using its original allocator. Self-move does nothing. The noexcept declarations follow comparator/allocator copy construction or assignment, including comparator adapters, and propagate through every public container wrapper. Copy operations and member swap retain their existing behavior.

This preserves the existing allocator-propagation convention rather than adding standard unequal-nonpropagating relocation semantics. The move fixture disables node allocation and mapped-value copying, moves populated containers across distinct allocator states, verifies exact node/value identities and deallocation ownership, then reuses both emptied sources. All four public families retain copy support. Throwing comparator preparation checks source ownership and empty-destination recovery.
