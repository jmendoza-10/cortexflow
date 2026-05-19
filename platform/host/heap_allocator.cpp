// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
// Host backend: `HeapAllocator` implementation + `default_allocator()`
// selector. Uses the C++ global `::operator new` / `::operator delete` with
// alignment, guarded by `std::mutex` (architecture §6.5).
//
// The class itself is declared in `<cortexflow/messaging.hpp>` (core); only
// the impl is target-specific. POSIX provides an identically-named class
// backed by `posix_memalign` + `pthread_mutex_t` in `platform/posix/`.

#include <cortexflow/messaging.hpp>
#include <cortexflow/assert.hpp>

#include <mutex>
#include <new>

namespace cortexflow {

namespace {

std::mutex& heap_mutex() {
    // Intentionally never destroyed: callers may hold Envelopes in file-scope
    // statics whose destructors run after this function-local static would
    // otherwise be torn down, producing a use-after-destroy on shutdown.
    static std::mutex* m = new std::mutex();
    return *m;
}

} // anonymous namespace

void* HeapAllocator::allocate(std::size_t size, std::size_t alignment) {
    void* ptr = ::operator new(size, std::align_val_t{alignment}, std::nothrow);
    CORTEXFLOW_ASSERT(ptr != nullptr, "heap allocation failed");
    return ptr;
}

void HeapAllocator::deallocate(void* ptr, std::size_t /*size*/,
                               std::size_t alignment) {
    ::operator delete(ptr, std::align_val_t{alignment});
}

void HeapAllocator::lock() {
    heap_mutex().lock();
}

void HeapAllocator::unlock() {
    heap_mutex().unlock();
}

HeapAllocator& HeapAllocator::instance() {
    static HeapAllocator inst;
    return inst;
}

MessageAllocator& default_allocator() {
    return HeapAllocator::instance();
}

} // namespace cortexflow
