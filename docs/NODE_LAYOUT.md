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

## Range insertion

Range insertion reacquires its end hint after each mutation. This preserves iterator invalidation rules across first insertion, compact-root growth and splits. Coverage compares all four container families with standard ordered containers for empty and populated destinations, duplicate input, range construction and initializer-list insertion.

## Comparator checking

Return-type checking uses unevaluated const references, matching lookup. Explicitly constructed keys and comparators need no default constructor for this check; the bool versus tagged compare-to return contract remains unchanged. This changes no node storage or runtime comparator mechanism.

## First-root publication

The first value is constructed in an unpublished one-slot leaf. A failed construction frees that leaf while the container retains a null root. Successful construction publishes the root without a key search or comparator copy; other insertion paths are unchanged.

## Ordered copy ownership

Copy construction builds each value once in a compact ordered shape, with O(height) planning storage. Single-leaf copies use a stack plan through the same builder. Completed children and constructed value prefixes have independent guards; cleanup uses exact leaf/internal/root allocation extents without interpreting an unpublished root sentinel. Copies preserve duplicate order and alignment.

Copy failure releases all newly owned nodes and values while preserving the source. Assignment clears with the previous allocator, then follows the established comparator/allocator propagation convention. Failed value construction leaves an empty destination; preserving its previous contents is not promised.

## Guarded compact-root growth

A full compact leaf root grows into one unpublished geometric-capacity replacement. Construct incoming arguments first at their final position to preserve contained lvalue aliases. A separate incoming flag covers that slot until the contiguous constructed prefix reaches it. Original values use `move_if_noexcept`; failure destroys any disjoint incoming value, the counted prefix and the exact replacement extent. Publish only after all values exist, then destroy the original root.

Copyable values retain contents on failure when copied, and nonthrowing moves leave no later fallible transfer. Throwing move-only mapped values may remain moved-from with keys and topology valid. If a noncopyable set-key move is attempted and fails, clean the replacement then clear the original set canonically. Explicit contained mapped moves can consume their source; moving contained set keys is unsupported. Growth uses no swaps, planner or scratch allocation.

## Nonfull insertion accounting

An internal root increments its size only after nonfull leaf insertion succeeds. Incoming-constructor refusal on direct append preserves the previous size; retry can reuse the existing leaf without allocation.

## Value slots and relocation

A set slot holds its key. A map slot holds a live `std::pair<Key, T>`, the mutable pair, and `reference` exposes it as `std::pair<const Key, T>&` through the union `btree_map_params::slot_type` (`value` and `mutable_value` members), the technique Abseil's `map_slot_type` and libc++ use. Splitting, rebalancing, merging, erasing and root replacement relocate a value by move-constructing the mutable pair at the destination and destroying the source, so a key moves instead of being copied. `params::swap` swaps the mutable pair; no `const_cast` is involved. Temporaries built for emplacement are mutable pairs for the same reason.

The exposure is implementation-defined, not guaranteed by the standard. Compile-time checks gate it: `btree_map_slot_layout` requires identical size and alignment for both pair types and identical member offsets for mirror structs of `Key`/`T` with and without a const key, and the slot union must have the value's size and alignment. A library whose two pair types differ fails to compile; there is no fallback.

When `std::is_nothrow_move_constructible` holds for the mutable pair (`std::string`, variants and nested maps of such types), relocation cannot throw. Every allocation then precedes the first value move:

- `internal_emplace` constructs the incoming value before reshaping the tree, so a refused construction leaves the tree untouched and arguments aliasing contained values are read first.
- `rebalance_or_split` allocates each level's sibling before recursing into the parent, from the leaf upward, and allocates the new root, if any, before the root is reshaped. A refusal at any level releases the sibling nodes already allocated.
- Erase, merge, root collapse and node swap allocate nothing.

An insertion that is refused leaves contents, structure and live allocations unchanged. Middle insertion into a nonfull leaf shifts values by move into the gap.

Types whose move may throw keep the earlier behavior: the incoming value is built first for compact-root growth, later relocation may throw midway through a split, rebalance or merge, and no recovery is attempted. Growth of a compact root uses `std::move_if_noexcept`, and a throwing move without a copy fallback clears the container on failure when keys may have been consumed.

## Ownership moves

Root links are node-relative, so moving ownership needs no traversal or reparenting. Copy comparator and internal-allocator state before transferring the root; a preparation failure leaves the source owning its nodes. Move assignment first clears the destination with its previous allocator, then adopts source state and root. A failed assignment leaves an empty destination. This retains the existing source-allocator adoption convention for unequal allocators.

The moved source is empty and reusable, and transferred element addresses and node capacity remain unchanged. Self-move preserves contents; const rvalues copy. Conditional `noexcept` depends on comparator/internal-allocator copy operations, independently of value movement. Copy and member-swap behavior remain unchanged.
