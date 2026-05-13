#include <cortexflow/messaging.hpp>
#include <cortexflow/assert.hpp>

#include <mutex>
#include <new>

namespace cortexflow {

namespace {

std::mutex& heap_mutex() {
    static std::mutex m;
    return m;
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
