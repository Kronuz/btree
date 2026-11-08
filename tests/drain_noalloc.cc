#include <btree/map.h>
#include <btree/set.h>
#include <cstdlib>
#include <new>
#include <string>

namespace {
bool refuse_allocation = false;
void require(bool condition) { if (!condition) std::abort(); }
}
void* operator new(std::size_t count) {
	if (refuse_allocation) throw std::bad_alloc();
	if (auto pointer = std::malloc(count ? count : 1)) return pointer;
	throw std::bad_alloc();
}
void* operator new[](std::size_t count) { return ::operator new(count); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new(std::size_t count, std::align_val_t alignment) {
	if (refuse_allocation) throw std::bad_alloc();
	void* pointer = nullptr;
	if (!posix_memalign(&pointer, static_cast<std::size_t>(alignment), count ? count : 1)) return pointer;
	throw std::bad_alloc();
}
void* operator new[](std::size_t count, std::align_val_t alignment) { return ::operator new(count, alignment); }
void operator delete(void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { std::free(pointer); }
struct alignas(64) Key {
	int value;
	explicit Key(int number) : value(number) {}
	bool operator<(const Key& other) const { return value < other.value; }
};
int main() {
	{
		using Map = btree::map<std::string, std::string>;
		Map::drain_type held;
		{
			Map source;
			for (unsigned key = 0; key < 1024; ++key) source.emplace(std::to_string(key), std::string(128, 'v'));
			refuse_allocation = true;
			held = source.drain();
			require(held.step(1) == 1 && !held.done());
			refuse_allocation = false;
			for (unsigned key = 0; key < 1024; ++key) source.emplace(std::to_string(key), std::string(128, 'x'));
			refuse_allocation = true;
			source.clear(); require(source.empty() && !held.done());
		}
		while (!held.done()) require(held.step(1) == 1);
		refuse_allocation = false;
	}
	{
		btree::map<std::string, std::string> values, other;
		for (unsigned key = 0; key < 4096; ++key) {
			values.emplace(std::string(80, 'k') + std::to_string(key), std::string(128, 'v'));
			if (key < 64) other.emplace(std::string(80, 'o') + std::to_string(key), std::string(128, 'x'));
		}
		refuse_allocation = true;
		{
			decltype(values)::drain_type empty;
			require(empty.done() && empty.step(0) == 0);
			auto cursor = values.drain();
			auto replaced = other.drain();
			require(values.empty() && other.empty() && cursor.step(0) == 0);
			require(cursor.step(1) == 1);
			replaced = std::move(cursor);
			require(cursor.done());
			empty = std::move(replaced);
			require(replaced.done());
			for (unsigned step = 0; step < 17 && !empty.done(); ++step) require(empty.step(1) == 1);
		}
		refuse_allocation = false;
		values.emplace("reused", "value"); values.verify();
	}
	{
		btree::set<Key> values;
		for (unsigned key = 0; key < 512; ++key) values.emplace(key);
		refuse_allocation = true;
		{
			auto cursor = values.drain();
			while (!cursor.done()) require(cursor.step(1) == 1);
		}
		refuse_allocation = false;
	}
}
