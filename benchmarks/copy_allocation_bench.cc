// Compile the same source against each compared include tree.
#include "allocation_meter.hh"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using btree_bench::AllocationMeter;
using btree_bench::MeterAllocator;
using btree_bench::Map;

template <typename Tree>
void measure(const char* name, const Tree& source, const std::shared_ptr<AllocationMeter>& meter, unsigned repeats) {
	const auto baseline = meter->live, calls = meter->calls, requested = meter->requested;
	meter->peak = baseline;
	std::size_t retained = 0;
	const auto start = std::clock();
	for (unsigned repeat = 0; repeat < repeats; ++repeat) {
		{
			auto copied = source;
			if (copied.size() != source.size()) throw std::runtime_error("copy size mismatch");
			retained = meter->live - baseline;
		}
		if (meter->live != baseline) throw std::runtime_error("copy allocation leak");
	}
	const auto elapsed = 1000.0 * (std::clock() - start) / CLOCKS_PER_SEC;
	std::printf("{\"workload\":\"%s\",\"entries\":%zu,\"repeats\":%u,\"source_live_requested_bytes\":%zu,\"retained_copy_requested_bytes\":%zu,\"peak_extra_requested_bytes\":%zu,\"allocation_calls_per_copy\":%zu,\"requested_bytes_per_copy\":%zu,\"process_cpu_ms\":%.6f}\n",
		name, static_cast<std::size_t>(source.size()), repeats, baseline, retained, meter->peak - baseline,
		(meter->calls - calls) / repeats, (meter->requested - requested) / repeats, elapsed);
}
int main() {
	auto meter = std::make_shared<AllocationMeter>();
	for (unsigned entries : {1u, 8u, 65536u}) {
		Map<std::string> source(std::less<std::string>{}, MeterAllocator<std::pair<const std::string, std::string>>{meter});
		std::vector<unsigned> order(entries);
		std::iota(order.begin(), order.end(), 0);
		std::mt19937 random(7106);
		std::shuffle(order.begin(), order.end(), random);
		for (auto index : order) source.emplace("group" + std::to_string(1000000 + index), std::string(80, 'x'));
		measure(entries == 1 ? "tiny-1" : entries == 8 ? "tiny-8" : "full", source, meter, entries < 10 ? 10000 : 4);
		if (entries == 65536) {
			for (auto index : order) if (index % 4) source.erase("group" + std::to_string(1000000 + index));
			measure("pruned", source, meter, 4);
		}
	}
	if (meter->live != 0) return 1;
	{
		using Inner = Map<std::string>;
		Map<Inner> source(std::less<std::string>{}, MeterAllocator<std::pair<const std::string, Inner>>{meter});
		for (unsigned outer = 0; outer < 8; ++outer) {
			Inner inner(std::less<std::string>{}, MeterAllocator<std::pair<const std::string, std::string>>{meter});
			for (unsigned key = 0; key < 8; ++key) inner.emplace("key" + std::to_string(key), std::string(80, 'x'));
			source.emplace("outer" + std::to_string(outer), std::move(inner));
		}
		measure("nested-8x8", source, meter, 1000);
	}
	return meter->live == 0 ? 0 : 1;
}
