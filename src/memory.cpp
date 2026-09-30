#include "memory.h"
#include <cstdlib>
#include <new>
#include <atomic>

// Thread-safe memory counter
std::atomic<size_t> g_used_memory(0);

size_t get_used_memory() {
    return g_used_memory.load();
}

void* custom_alloc(size_t size) {
    void* p = malloc(size + sizeof(size_t));
    if (!p) throw std::bad_alloc();
    *static_cast<size_t*>(p) = size;
    g_used_memory += size;
    return static_cast<char*>(p) + sizeof(size_t);
}

void custom_free(void* p) {
    if (!p) return;
    void* raw = static_cast<char*>(p) - sizeof(size_t);
    size_t size = *static_cast<size_t*>(raw);
    g_used_memory -= size;
    free(raw);
}

// Override all scalar and array forms
void* operator new(size_t size) { return custom_alloc(size); }
void* operator new[](size_t size) { return custom_alloc(size); }

void operator delete(void* p) noexcept { custom_free(p); }
void operator delete[](void* p) noexcept { custom_free(p); }

// Override C++14 sized deallocations
void operator delete(void* p, size_t /*size*/) noexcept { custom_free(p); }
void operator delete[](void* p, size_t /*size*/) noexcept { custom_free(p); }