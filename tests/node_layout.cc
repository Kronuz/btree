#include "btree/map.h"
#include "btree/set.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <new>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "node_layout/fixtures.hh"
#include "node_layout/compact_roots.hh"
#include "node_layout/copy_failures.hh"
#include "node_layout/root_growth.hh"
#include "node_layout/empty_insert.hh"
#include "node_layout/prepared.hh"

int main(int argc, char** argv) {
	try {
		if (argc == 2 && std::string_view(argv[1]) == "--short-root-only") {
			short_root_lifetime();
		} else if (argc == 2 && std::string_view(argv[1]) == "--prepared-forwarding-only") {
			prepared_forwarding();
		} else if (argc == 2 && std::string_view(argv[1]) == "--nonfull-append-only") {
			nonfull_append_failure();
		} else {
			require(argc == 1);
			short_root_lifetime(); compact_roots(); unpadded_leaf_extents(); move_only_values();
			aligned_slots(); allocation_failure(); rejected_alignment(); ordered_model(); range_insertions(); comparator_expression(); first_insert_cleanup();
			compact_copy_boundaries(); copy_failure_positions(); compact_copy_history();
			assignment_allocation_failure_positions(); assignment_failure_positions();
			nested_copy_failure_positions(); aligned_and_duplicate_copies();
			nonfull_leaf_failures(); nonfull_append_failure(); prepared_key_failures(); prepared_forwarding();
			root_growth_failures(); root_growth_aliases(); root_growth_move_only_recovery(); root_growth_move_only_mapped(int_key); root_growth_move_only_mapped(long_key);
		}
	}
	catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
	std::cout << checks << " compact layout checks passed\n";
}
