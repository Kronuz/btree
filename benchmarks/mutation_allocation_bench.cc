// Compile the same source against each compared include tree.
#include "allocation_meter.hh"
#include <cstdio>
#include <ctime>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

using btree_bench::AllocationMeter;
using btree_bench::Map;
using btree_bench::MeterAllocator;

struct CopyFallback {
	std::string data;
	explicit CopyFallback(std::string value) : data(std::move(value)) {}
	CopyFallback(const CopyFallback&) = default;
	CopyFallback& operator=(const CopyFallback&) = default;
	CopyFallback(CopyFallback&& other) noexcept(false) : data(std::move(other.data)) {}
	CopyFallback& operator=(CopyFallback&& other) noexcept(false) { data = std::move(other.data); return *this; }
};
struct Phase {
	const AllocationMeter& meter;
	std::size_t initial, calls, requested;
	std::clock_t start;
	explicit Phase(AllocationMeter& counter)
		: meter(counter), initial(counter.live), calls(counter.calls), requested(counter.requested), start(std::clock()) {
		counter.peak = counter.live;
	}
	void finish(const char* name, const char* operation, unsigned entries) const {
		const auto cpu = 1000.0 * (std::clock() - start) / CLOCKS_PER_SEC;
		std::printf("{\"workload\":\"%s-%s\",\"entries\":%u,\"start_live_requested_bytes\":%zu,\"end_live_requested_bytes\":%zu,\"peak_live_requested_bytes\":%zu,\"allocation_calls\":%zu,\"requested_bytes\":%zu,\"process_cpu_ms\":%.6f}\n",
			name, operation, entries, initial, meter.live, meter.peak, meter.calls - calls, meter.requested - requested, cpu);
	}
};
template <class Value, class Factory, class Verify>
void run(const char* name, unsigned entries, bool long_key, bool ordered, Factory factory, Verify verify) {
	auto meter = std::make_shared<AllocationMeter>();
	{
		Map<Value> values(std::less<std::string>{}, MeterAllocator<std::pair<const std::string, Value>>{meter});
		std::vector<unsigned> order(entries);
		std::vector<std::string> keys;
		std::iota(order.begin(), order.end(), 0);
		std::mt19937 random(7106);
		if (!ordered) std::shuffle(order.begin(), order.end(), random);
		keys.reserve(entries);
		for (unsigned i = 0; i < entries; ++i)
			keys.push_back((long_key ? "ranking:feature:" + std::string(64, 'k') + ":" : "group") + std::to_string(1000000 + i));
		{
			Phase phase(*meter);
			for (auto index : order) values.emplace(keys[index], factory(meter, index));
			phase.finish(name, "insert", entries);
		}
		if (values.size() != entries) throw std::runtime_error("insert size mismatch");
		for (const auto& value : values) verify(value.second);
		{
			Phase phase(*meter);
			for (auto index : order)
				if (values.erase(keys[index]) != 1) throw std::runtime_error("erase result mismatch");
			phase.finish(name, "erase", entries);
		}
		if (!values.empty()) throw std::runtime_error("erase size mismatch");
	}
	if (meter->live != 0) throw std::runtime_error("mutation allocation leak");
}
int main() {
	constexpr unsigned entries = 65536;
	auto text = [](const auto&, unsigned) { return std::string(80, 'v'); };
	auto check_text = [](const std::string& value) {
		if (value != std::string(80, 'v')) throw std::runtime_error("mapped value mismatch");
	};
	run<std::string>("short-random", entries, false, false, text, check_text);
	run<std::string>("long-random", entries, true, false, text, check_text);
	run<std::string>("long-ordered", entries, true, true, text, check_text);
	run<CopyFallback>("generic-long-random", entries, true, false,
		[](const auto&, unsigned) { return CopyFallback(std::string(80, 'v')); },
		[&](const CopyFallback& value) { check_text(value.data); });
	using Inner = Map<std::string>;
	run<Inner>("nested-8-long-random", entries / 8, true, false,
		[](const std::shared_ptr<AllocationMeter>& meter, unsigned) {
			Inner inner(std::less<std::string>{}, MeterAllocator<std::pair<const std::string, std::string>>{meter});
			for (unsigned key = 0; key < 8; ++key)
				inner.emplace("host-group:" + std::string(64, 'g') + std::to_string(key), std::string(80, 'v'));
			return inner;
		}, [&](const Inner& inner) {
			if (inner.size() != 8) throw std::runtime_error("nested size mismatch");
			for (const auto& value : inner) check_text(value.second);
		});
}
