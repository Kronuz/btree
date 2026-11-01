# btree API contracts

`btree::map`, `btree::set`, `btree::multimap` and `btree::multiset` follow the interfaces of
the C++17 standard ordered containers, with the differences and guarantees below. The node
mechanics are in [NODE_LAYOUT.md](NODE_LAYOUT.md).

## Iterators

Any insertion or erase can split, merge or rebalance nodes, and moves values within a node
even when it does not, so it invalidates every iterator and reference into the container,
not only those to the erased element. `erase(iterator)` returns an iterator to the element
after the erased one.

## Keys move

A `btree::map` slot stores a mutable `std::pair<Key, T>` and hands it out as
`std::pair<const Key, T>&`, so splitting, rebalancing, merging and erasing move keys instead
of copying them. With nothrow-movable keys and mapped values, such as `std::string`, an
insertion allocates only its new nodes and the incoming value, and an erase allocates
nothing. Maps accept move-only keys.

## Failure guarantees

When the key and mapped value move without throwing (`std::string`, integers, variants and
nested `btree::map`s of such types) and the pair fits the target node size (256 bytes by
default), insertion and erase are failure-atomic for allocation:

- **Insertion** constructs the incoming value first, then allocates every node a split
  needs, from the leaf up to a new root, before it moves any value. A refused allocation or a
  throwing value constructor leaves the contents, the structure and the live allocations
  unchanged.
- **Erase**, merging and root collapse allocate nothing.

Pairs larger than the target node size, and types whose move may throw, have narrower
guarantees. A throw from a move in the middle of a full-node split, rebalance or merge is not
recovered.

- **Full-leaf insertion** splits first and then constructs the value in its slot. If the
  construction fails, the leaf the split left short is merged or rebalanced as erase does:
  the contents, the size and the structure stay valid, and the split may remain.
- **Nonfull middle insertion** and **compact leaf-root growth** construct the incoming value
  first in its final replacement slot, then relocate existing values under counted
  ownership. Failed copying leaves the original contents intact; a move-only mapped value
  whose move throws can remain moved-from, with every key in place. If a destructive move of
  a noncopyable key fails, the container becomes empty and reusable. Explicit moves from
  contained mapped arguments may consume them.

For every type:

- **First insertion** publishes its root only after constructing the value. An allocation or
  value-construction failure leaves a reusable empty container.
- **Nonfull leaf insertion** publishes the internal-root size after the value is constructed.
  A refused append leaves the previous size intact, and a retry reuses the existing capacity.
- **Copies** build a compact ordered shape without key comparisons and release every
  completed value and node on failure, including nested containers. Copy assignment clears,
  then copies: a failed copy leaves the destination empty.
- **Range insertion and range construction** refresh their end hint after each insertion,
  including root growth and splits. The compact-layout suite compares all four container
  families with standard ordered containers for duplicate input and populated destinations.

## Comparators

Comparator return-type checking uses const references and does not require a
default-constructible key. Comparators return `bool`, or `int` with the compare-to tag. A
comparator with a nested `is_transparent` type (such as `std::less<>`) enables heterogeneous
lookup, as in the standard containers.

## Ownership moves

All four containers transfer their root when constructed or assigned from a non-const
rvalue. The source becomes empty and reusable; node allocations and element addresses
transfer unchanged. Construction copies comparator and allocator state before taking
ownership. Assignment first clears the destination with its previous allocator, then copies
source state and takes its root. Assignment follows the library's source-allocator adoption
convention, including for unequal stateful allocators.

If state preparation throws, the source keeps its nodes. A failed move assignment leaves the
destination empty. Self-move preserves the container; const rvalues copy. Conditional
`noexcept` follows actual comparator and internal-allocator copy operations, independently of
element movement. Populated-destination assignment includes destruction work and is not
constant-time. Copy and member swap do not use this path. Rebuild every consumer against the
same header revision.

`tests/ownership_move.cc` checks exact identities and allocation ownership through all four
families, over-alignment, source reuse, and comparator and allocator failures.
`benchmarks/ownership_bench.cc` measures construction, assignment into empty destinations,
and assignment into populated destinations separately. The
[actual-model report](https://github.com/Kronuz/Detent/blob/main/benchmarks/btree-ownership-moves-20261022.json)
includes string heaps and states the remaining qualification gates.

## Layout-checked slots

A map slot is a union of `std::pair<Key, T>` and `std::pair<const Key, T>`, read through the
const-key member while the mutable member is live, as in Abseil's btree. The standard leaves
this layout implementation-defined; static assertions on size, alignment and member offsets
stop the build on a library where the two pairs differ.

## Known limitations

Nested containers with non-default-constructible allocators can fail constructor-trait
instantiation under Clang with libstdc++ 13 in C++20. The optional nested allocator benchmark
hits the same limitation on some compiler and library combinations.
