#include "btree/map.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

namespace {
// The replaced operators below share this state with main. It is volatile
// because GCC 16 assumes replaceable new/delete touch no global state
// (-fassume-sane-operators-new-delete, on by default) and would otherwise
// cache counter reads and drop budget stores across allocations.
volatile long allocation_budget = -1;
volatile long live_allocations = 0;
volatile bool deny_heap = false;
volatile unsigned denied_requests = 0;
void check(bool condition) { if (!condition) std::abort(); }
struct IncomingRefusal {};
struct Value {
    static inline const Value* arm_at = nullptr;
    static inline bool refuse_incoming = false;
    int number;
    explicit Value(int input) : number(input) {
        if (refuse_incoming) throw IncomingRefusal{};
    }
    Value(const Value&) = default;
    Value(Value&& other) noexcept : number(other.number) {
        if (&other == arm_at) deny_heap = true;
        other.number = -1;
    }
    Value& operator=(const Value&) = default;
    Value& operator=(Value&&) = default;
};
}
namespace {
void admit_allocation() {
    if (deny_heap) { denied_requests = denied_requests + 1; throw std::bad_alloc(); }
    if (allocation_budget == 0) throw std::bad_alloc();
    if (allocation_budget > 0) allocation_budget = allocation_budget - 1;
}
}
void* operator new(std::size_t bytes) {
    admit_allocation();
    if (auto* pointer = std::malloc(bytes ? bytes : 1)) { live_allocations = live_allocations + 1; return pointer; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void* operator new(std::size_t bytes, std::align_val_t alignment) {
    admit_allocation();
    void* pointer = nullptr;
    if (posix_memalign(&pointer, static_cast<std::size_t>(alignment), bytes ? bytes : 1)) throw std::bad_alloc();
    live_allocations = live_allocations + 1;
    return pointer;
}
void* operator new[](std::size_t bytes, std::align_val_t alignment) { return ::operator new(bytes, alignment); }

void operator delete(void* pointer) noexcept {
    if (pointer) { live_allocations = live_allocations - 1; std::free(pointer); }
}
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { ::operator delete(pointer); }

int main() {
    const auto initial = live_allocations;
    {
        const auto zero = std::string(127, 'k') + '0';
        const auto two = std::string(127, 'k') + '2';
        const auto four = std::string(127, 'k') + '4';
        const auto eight = std::string(127, 'k') + '8';
        bool complete = false;
        int refusals = 0;
        for (long budget = 0; budget < 32; ++budget) {
            btree::map<std::string, Value> values;
            values.try_emplace(zero, 0); values.try_emplace(four, 4); values.try_emplace(eight, 8);
            const auto before = live_allocations;
            Value::arm_at = &values.find(eight)->second;
            allocation_budget = budget;
            bool refused = false;
            try { values.try_emplace(two, 2); }
            catch (const std::bad_alloc&) { refused = true; }
            const bool armed = deny_heap;
            deny_heap = false; allocation_budget = -1; Value::arm_at = nullptr;
            check(denied_requests == 0);
            check(values.at(zero).number == 0 && values.at(four).number == 4 && values.at(eight).number == 8);
            if (refused) {
                ++refusals;
                check(!armed && values.size() == 3 && live_allocations == before);
            } else {
                check(armed && values.size() == 4 && values.at(two).number == 2);
                complete = true;
            }
            values.verify();
            if (complete) break;
        }
        check(complete && refusals > 0);
        btree::map<std::string, Value> values;
        values.try_emplace(zero, 0); values.try_emplace(four, 4); values.try_emplace(eight, 8);
        const auto before = live_allocations;
        Value::arm_at = &values.find(eight)->second;
        Value::refuse_incoming = true;
        bool refused = false;
        try { values.try_emplace(two, 2); } catch (const IncomingRefusal&) { refused = true; }
        Value::refuse_incoming = false; Value::arm_at = nullptr;
        check(refused && !deny_heap && values.size() == 3 && live_allocations == before);
        check(values.at(four).number == 4 && values.at(eight).number == 8); values.verify();
    }
    check(live_allocations == initial);
    std::puts("PASS prepared insertion complete-heap commitment and incoming refusal");
}
