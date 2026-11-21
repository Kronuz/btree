/*
 * Copyright (c) 2019-2026 Germán Méndez Bravo (Kronuz)
 * Copyright (c) 2013 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *
 * A btree implementation of the STL set and map interfaces. A btree is both
 * smaller and faster than STL set/map. The red-black tree implementation of
 * STL set/map has an overhead of 3 pointers (left, right and parent) plus the
 * node color information for each stored value. So a set<int32> consumes 20
 * bytes for each value stored. This btree implementation stores multiple
 * values on fixed size nodes (usually 256 bytes) and doesn't store child
 * pointers for leaf nodes. The result is that a btree::set<int32> may use much
 * less memory per stored value. For the random insertion benchmark in
 * btree_test.cc, a btree::set<int32> with node-size of 256 uses 4.9 bytes per
 * stored value.
 *
 * The packing of multiple values on to each node of a btree has another effect
 * besides better space utilization: better cache locality due to fewer cache
 * lines being accessed. Better cache locality translates into faster
 * operations.
 *
 * CAVEATS
 *
 * Insertions and deletions on a btree can cause splitting, merging or
 * rebalancing of btree nodes. And even without these operations, insertions
 * and deletions on a btree will move values around within a node. In both
 * cases, the result is that insertions and deletions can invalidate iterators
 * pointing to values other than the one being inserted/deleted. This is
 * notably different from STL set/map which takes care to not invalidate
 * iterators on insert/erase except, of course, for iterators pointing to the
 * value being erased.  A partial workaround when erasing is available:
 * erase() returns an iterator pointing to the item just after the one that was
 * erased (or end() if none exists).  See also safe_btree.
 *
 * PERFORMANCE
 *
 *   btree_bench --benchmarks=. 2>&1 | ./benchmarks.awk
 *
 * Run on pmattis-warp.nyc (4 X 2200 MHz CPUs); 2010/03/04-15:23:06
 * Benchmark                 STL(ns) B-Tree(ns) @    <size>
 * --------------------------------------------------------
 * BM_set_int32_insert        1516      608  +59.89%  <256>    [40.0,  5.2]
 * BM_set_int32_lookup        1160      414  +64.31%  <256>    [40.0,  5.2]
 * BM_set_int32_fulllookup     960      410  +57.29%  <256>    [40.0,  4.4]
 * BM_set_int32_delete        1741      528  +69.67%  <256>    [40.0,  5.2]
 * BM_set_int32_queueaddrem   3078     1046  +66.02%  <256>    [40.0,  5.5]
 * BM_set_int32_mixedaddrem   3600     1384  +61.56%  <256>    [40.0,  5.3]
 * BM_set_int32_fifo           227      113  +50.22%  <256>    [40.0,  4.4]
 * BM_set_int32_fwditer        158       26  +83.54%  <256>    [40.0,  5.2]
 * BM_map_int32_insert        1551      636  +58.99%  <256>    [48.0, 10.5]
 * BM_map_int32_lookup        1200      508  +57.67%  <256>    [48.0, 10.5]
 * BM_map_int32_fulllookup     989      487  +50.76%  <256>    [48.0,  8.8]
 * BM_map_int32_delete        1794      628  +64.99%  <256>    [48.0, 10.5]
 * BM_map_int32_queueaddrem   3189     1266  +60.30%  <256>    [48.0, 11.6]
 * BM_map_int32_mixedaddrem   3822     1623  +57.54%  <256>    [48.0, 10.9]
 * BM_map_int32_fifo           151      134  +11.26%  <256>    [48.0,  8.8]
 * BM_map_int32_fwditer        161       32  +80.12%  <256>    [48.0, 10.5]
 * BM_set_int64_insert        1546      636  +58.86%  <256>    [40.0, 10.5]
 * BM_set_int64_lookup        1200      512  +57.33%  <256>    [40.0, 10.5]
 * BM_set_int64_fulllookup     971      487  +49.85%  <256>    [40.0,  8.8]
 * BM_set_int64_delete        1745      616  +64.70%  <256>    [40.0, 10.5]
 * BM_set_int64_queueaddrem   3163     1195  +62.22%  <256>    [40.0, 11.6]
 * BM_set_int64_mixedaddrem   3760     1564  +58.40%  <256>    [40.0, 10.9]
 * BM_set_int64_fifo           146      103  +29.45%  <256>    [40.0,  8.8]
 * BM_set_int64_fwditer        162       31  +80.86%  <256>    [40.0, 10.5]
 * BM_map_int64_insert        1551      720  +53.58%  <256>    [48.0, 20.7]
 * BM_map_int64_lookup        1214      612  +49.59%  <256>    [48.0, 20.7]
 * BM_map_int64_fulllookup     994      592  +40.44%  <256>    [48.0, 17.2]
 * BM_map_int64_delete        1778      764  +57.03%  <256>    [48.0, 20.7]
 * BM_map_int64_queueaddrem   3189     1547  +51.49%  <256>    [48.0, 20.9]
 * BM_map_int64_mixedaddrem   3779     1887  +50.07%  <256>    [48.0, 21.6]
 * BM_map_int64_fifo           147      145   +1.36%  <256>    [48.0, 17.2]
 * BM_map_int64_fwditer        162       41  +74.69%  <256>    [48.0, 20.7]
 * BM_set_string_insert       1989     1966   +1.16%  <256>    [64.0, 44.5]
 * BM_set_string_lookup       1709     1600   +6.38%  <256>    [64.0, 44.5]
 * BM_set_string_fulllookup   1573     1529   +2.80%  <256>    [64.0, 35.4]
 * BM_set_string_delete       2520     1920  +23.81%  <256>    [64.0, 44.5]
 * BM_set_string_queueaddrem  4706     4309   +8.44%  <256>    [64.0, 48.3]
 * BM_set_string_mixedaddrem  5080     4654   +8.39%  <256>    [64.0, 46.7]
 * BM_set_string_fifo          318      512  -61.01%  <256>    [64.0, 35.4]
 * BM_set_string_fwditer       182       93  +48.90%  <256>    [64.0, 44.5]
 * BM_map_string_insert       2600     2227  +14.35%  <256>    [72.0, 55.8]
 * BM_map_string_lookup       2068     1730  +16.34%  <256>    [72.0, 55.8]
 * BM_map_string_fulllookup   1859     1618  +12.96%  <256>    [72.0, 44.0]
 * BM_map_string_delete       3168     2080  +34.34%  <256>    [72.0, 55.8]
 * BM_map_string_queueaddrem  5840     4701  +19.50%  <256>    [72.0, 59.4]
 * BM_map_string_mixedaddrem  6400     5200  +18.75%  <256>    [72.0, 57.8]
 * BM_map_string_fifo          398      596  -49.75%  <256>    [72.0, 44.0]
 * BM_map_string_fwditer       243      113  +53.50%  <256>    [72.0, 55.8]
 */

#ifndef BTREE_BTREE_H__
#define BTREE_BTREE_H__

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <ostream>
#include <string>
#include <stdexcept>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

#include "btree_compare.h"
#include "btree_params.h"
#include "btree_node.h"
#include "btree_iterator.h"
#include "btree_tree.h"
#include "btree_node_impl.h"
#include "btree_tree_modify.h"
#include "btree_tree_internal.h"
#include "btree_container.h"

#endif  // BTREE_BTREE_H__
