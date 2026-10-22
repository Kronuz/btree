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

Copy failure releases all newly owned nodes and values while preserving the source. Assignment clears with the previous allocator, then follows the established comparator/allocator propagation convention. Failed value construction leaves an empty destination; preserving its previous contents is not promised. This repair does not change general insertion, erase or const-key mutation behavior.
