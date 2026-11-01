# Compact node storage

Every node allocation contains a complete `btree_node` header. Values, internal child pointers and internal-root metadata occupy separately addressed trailing storage. No allocation is treated as a complete `leaf_fields`, `internal_fields` or `root_fields` object; those types serve only as size descriptors, kept for source compatibility.

The header holds the leaf flag, position, capacity, live count and parent pointer. Value slots begin at the header size rounded to value alignment. Child slots follow the full value capacity, rounded to pointer alignment. Root metadata follows the internal slots. The offset helpers define allocation, access, deallocation and `bytes_used()` together.

Construction creates the header, starts each internal pointer slot's lifetime without initializing its value, and creates metadata only for internal roots. Each value slot begins its lifetime when an insertion or movement constructs a value there. Only live values are read or destroyed. Child pointers and root metadata are trivially destructible; their lifetimes end when the backing storage is released. Inactive child values remain unspecified, independently of `NDEBUG`.

## Footprint and alignment

For ordinarily aligned values, a short-root request is `sizeof(header) + capacity * sizeof(value)`, with any required padding before values. Capacity grows geometrically. Full leaves use that same extent, so allocation and deallocation agree for every value size. Internal storage adds only the alignment needed by child pointers and root metadata.

Ordinary node requests rebind the supplied allocator to `char`. The returned block must align the node header and stored values; a misaligned block is returned to the allocator and rejected before construction. Values whose alignment exceeds `max_align_t` rebind the allocator to an aligned storage unit. Their request count and reported capacity include whole units, and deallocation uses the identical type and count.

The layout is internal: the public container operations do not depend on it. Node internals require a complete rebuild when changing header revisions; the mixed-mode tests cover Debug/Release combinations using the same revision. Custom allocators must support normal allocator rebinding, including aligned units when storing over-aligned values. Empty containers report only their object size in `bytes_used()`.

## Qualification

The counting-allocator test checks exact extents and matching deallocation, ordinary short-root capacities, unusually sized values, over-aligned values and allocation failure. Nontrivial values exercise construction, movement and destruction through splitting, merging and root collapse. Run it with sanitizer recovery disabled, then run the mixed-mode tests and downstream applications.

To qualify a layout change, benchmark it against the previous revision for allocation counts, peak requested bytes and mutation CPU. Compare identical keys, mapped values, insertion/erasure schedules and build flags. Treat an exact footprint change separately from timing variation. No performance result follows from source inspection alone.

## Range insertion

Range insertion reacquires its end hint after each mutation. This preserves iterator invalidation rules across first insertion, compact-root growth and splits. Coverage compares all four container families with standard ordered containers for empty and populated destinations, duplicate input, range construction and initializer-list insertion.

## Comparator checking

Return-type checking uses unevaluated const references, matching lookup. Explicitly constructed keys and comparators need no default constructor for this check. Comparators return `bool`, or `int` with the compare-to tag.

## First-root publication

The first value is constructed in an unpublished one-slot leaf. A failed construction frees that leaf while the container retains a null root. Successful construction publishes the root without a key search or comparator copy.

## Ordered copy ownership

Copy construction builds each value once in a compact ordered shape, with O(height) planning storage. Single-leaf copies use a stack plan through the same builder. Completed children and constructed value prefixes have independent guards; cleanup uses exact leaf/internal/root allocation extents without interpreting an unpublished root sentinel. Copies preserve duplicate order and alignment.

Copy failure releases all newly owned nodes and values while preserving the source. Assignment clears with the previous allocator, then follows the library's comparator/allocator propagation convention. Failed value construction leaves an empty destination; its previous contents are not preserved.

## Value slots

A set slot is its key. A map slot is a `btree_map_params::slot_type` union with two members over the same bytes: `mutable_value`, a `std::pair<Key, T>`, and `value`, a `std::pair<const Key, T>`. Construction begins the union's lifetime and then constructs `mutable_value` in place, so the live object is always the mutable pair. Iterators and references read `value`, which gives callers the `std::pair<const Key, T>&` that `value_type` promises. Relocation, swapping and destruction use `mutable_value`, so a key moves instead of being copied and no const object is ever modified. Temporaries built for emplacement are mutable pairs for the same reason. This is the object model of Abseil's `map_slot_type`.

Reading one union member while the other is live is type punning through a union. The standard permits it only for the common initial sequence of standard-layout members; GCC and Clang document it as supported in general. Compile-time checks gate the layout: `btree_map_slot_layout` requires both pair types to have the same size and alignment, and mirror structs of `Key` and `T` with and without a const key to have the same member offsets; the union must have the size and alignment of `value_type`. A library whose two pair types differ fails to compile; there is no fallback.

## Relocation and allocation order

When the mutable pair is nothrow move constructible (`std::string`, integers, variants and nested `btree::map`s of such types), relocation cannot throw, and every allocation of an insertion precedes its first value move:

- `internal_emplace` constructs the incoming value, in automatic storage bounded by the target node size, before it reshapes the tree. A refused construction leaves the tree untouched, and arguments that alias contained values are read before anything moves.
- `rebalance_or_split` allocates each level's sibling before it recurses into the parent, from the leaf upward, and allocates the new root, if any, before it reshapes the root. A refusal at any level releases the siblings already allocated.
- Nonfull middle insertion shifts the tail through one empty slot by move. The commit makes no library allocation or comparison. Append stays direct.
- Erase, merge, root collapse and node swap allocate nothing.

A refused insertion leaves contents, structure and live allocations unchanged.

Larger pairs, and pairs whose move may throw, construct the incoming value in its slot after the split. The split biases its values away from the insertion point, so the target leaf can be empty until the value arrives. If the construction fails, the leaf is merged or rebalanced as erase does, and the size counts the value only once it exists: contents, size and structure stay valid, and the split may remain. A throw from a relocation in the middle of a split, rebalance or merge is not recovered.

## Guarded compact-root growth

A full compact leaf root grows into one unpublished geometric-capacity replacement. Nonfull middle insertion uses the same replacement when its pair is not nothrow movable or is larger than the target node size. Incoming arguments are constructed first at their final position, which preserves contained lvalue aliases. A separate incoming flag covers that slot until the contiguous constructed prefix reaches it. Failure destroys any disjoint incoming value, the counted prefix and the exact replacement extent. The replacement is published only after all values exist; then the original is destroyed.

Original values relocate by `move_if_noexcept` on the mutable pair, so copyable values are copied and keep the original contents on failure, and nonthrowing moves leave no later fallible transfer. A map whose mapped type is move-only and may throw on move relocates through the const-key view instead: the key is copied, the mapped value is moved, and on failure every key stays in place while the mapped value that threw may remain moved-from. A key that cannot be copied is consumed by its move, so if such a move fails the container is cleared canonically after the replacement is cleaned up. Explicit contained mapped moves can consume their source; moving contained set keys is unsupported. Growth uses no swaps, planner or scratch allocation.

## Nonfull insertion accounting

An internal root increments its size only after nonfull leaf insertion succeeds. Incoming-constructor refusal on direct append preserves the previous size; retry can reuse the existing leaf without allocation.

## Ownership moves

Root links are node-relative, so moving ownership needs no traversal or reparenting. Copy comparator and internal-allocator state before transferring the root; a preparation failure leaves the source owning its nodes. Move assignment first clears the destination with its previous allocator, then adopts source state and root. A failed assignment leaves an empty destination. Unequal allocators follow the library's source-allocator adoption convention.

The moved source is empty and reusable, and transferred element addresses and node capacity remain unchanged. Self-move preserves contents; const rvalues copy. Conditional `noexcept` depends on comparator/internal-allocator copy operations, independently of value movement. Copy and member swap do not use this path.
