// POSIX backend: `HeapAllocator` implementation using `posix_memalign` for
// allocation and `pthread_mutex_t` for the cross-thread lock that protects
// foreign-thread `make_message_with(...)` calls (architecture §6.5, queue
// concurrency story 47).
//
// Functionally equivalent to the host backend; the choice of POSIX primitives
// is the typedef-swap demonstration. Same `HeapAllocator` class declaration
// in `<cortexflow/messaging.hpp>`; only the .cpp differs.

#include <cortexflow/messaging.hpp>
#include <cortexflow/assert.hpp>

#include <cstdlib>
#include <pthread.h>

namespace cortexflow {

namespace {

// Heap-allocated, never destroyed: see host backend for the rationale —
// statics holding Envelopes can outlive a function-local static of mutex
// type. PTHREAD_MUTEX_INITIALIZER lets us avoid even that, but we
// pthread_mutex_init explicitly so the choice (recursive vs normal) is
// visible if it ever needs to change.
pthread_mutex_t& heap_mutex() {
    static pthread_mutex_t* m = [] {
        auto* p = new pthread_mutex_t;
        pthread_mutex_init(p, nullptr);
        return p;
    }();
    return *m;
}

} // anonymous namespace

void* HeapAllocator::allocate(std::size_t size, std::size_t alignment) {
    // `posix_memalign` requires alignment >= sizeof(void*) and a power of two.
    // Round up to satisfy the first constraint without altering correctness;
    // the second is guaranteed by C++ alignof requirements on any T.
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }
    void* ptr = nullptr;
    int rc = ::posix_memalign(&ptr, alignment, size);
    CORTEXFLOW_ASSERT(rc == 0 && ptr != nullptr,
                      "posix_memalign failed");
    return ptr;
}

void HeapAllocator::deallocate(void* ptr, std::size_t /*size*/,
                               std::size_t /*alignment*/) {
    ::free(ptr);
}

void HeapAllocator::lock() {
    pthread_mutex_lock(&heap_mutex());
}

void HeapAllocator::unlock() {
    pthread_mutex_unlock(&heap_mutex());
}

HeapAllocator& HeapAllocator::instance() {
    static HeapAllocator inst;
    return inst;
}

MessageAllocator& default_allocator() {
    return HeapAllocator::instance();
}

} // namespace cortexflow
