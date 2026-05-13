#pragma once

#include <cstddef>
#include <new>
#include <utility>

#include <cortexflow/type_name.hpp>

namespace cortexflow {

inline constexpr type_id_t kNoSender = 0;

class MessageAllocator {
public:
    virtual void* allocate(std::size_t size, std::size_t alignment) = 0;
    virtual void deallocate(void* ptr, std::size_t size, std::size_t alignment) = 0;
    virtual void lock() = 0;
    virtual void unlock() = 0;

protected:
    ~MessageAllocator() = default;
};

MessageAllocator& default_allocator();

template <typename T>
class MessagePtr {
public:
    MessagePtr() noexcept : ptr_(nullptr), allocator_(nullptr) {}

    MessagePtr(T* ptr, MessageAllocator* allocator) noexcept
        : ptr_(ptr), allocator_(allocator) {}

    ~MessagePtr() { destroy(); }

    MessagePtr(MessagePtr&& other) noexcept
        : ptr_(other.ptr_), allocator_(other.allocator_) {
        other.ptr_ = nullptr;
        other.allocator_ = nullptr;
    }

    MessagePtr& operator=(MessagePtr&& other) noexcept {
        if (this != &other) {
            destroy();
            ptr_ = other.ptr_;
            allocator_ = other.allocator_;
            other.ptr_ = nullptr;
            other.allocator_ = nullptr;
        }
        return *this;
    }

    MessagePtr(const MessagePtr&) = delete;
    MessagePtr& operator=(const MessagePtr&) = delete;

    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T* get() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    void destroy() {
        if (ptr_) {
            ptr_->~T();
            allocator_->deallocate(ptr_, sizeof(T), alignof(T));
            ptr_ = nullptr;
        }
    }

    T* ptr_;
    MessageAllocator* allocator_;

    friend class Envelope;
};

class Envelope {
public:
    template <typename T>
    Envelope(type_id_t from, type_id_t to, MessagePtr<T>&& msg)
        : from_(from)
        , to_(to)
        , type_id_(type_id<T>())
        , payload_(msg.ptr_)
        , allocator_(msg.allocator_)
        , destroy_fn_(&destroy_impl<T>) {
        msg.ptr_ = nullptr;
        msg.allocator_ = nullptr;
    }

    ~Envelope() {
        if (payload_) {
            destroy_fn_(allocator_, payload_);
        }
    }

    Envelope(Envelope&& other) noexcept
        : from_(other.from_)
        , to_(other.to_)
        , type_id_(other.type_id_)
        , payload_(other.payload_)
        , allocator_(other.allocator_)
        , destroy_fn_(other.destroy_fn_) {
        other.payload_ = nullptr;
    }

    Envelope& operator=(Envelope&& other) noexcept {
        if (this != &other) {
            if (payload_) {
                destroy_fn_(allocator_, payload_);
            }
            from_ = other.from_;
            to_ = other.to_;
            type_id_ = other.type_id_;
            payload_ = other.payload_;
            allocator_ = other.allocator_;
            destroy_fn_ = other.destroy_fn_;
            other.payload_ = nullptr;
        }
        return *this;
    }

    Envelope(const Envelope&) = delete;
    Envelope& operator=(const Envelope&) = delete;

    type_id_t from() const noexcept { return from_; }
    type_id_t to() const noexcept { return to_; }
    type_id_t payload_type_id() const noexcept { return type_id_; }

    template <typename T>
    T& payload() { return *static_cast<T*>(payload_); }

    template <typename T>
    const T& payload() const { return *static_cast<const T*>(payload_); }

private:
    template <typename T>
    static void destroy_impl(MessageAllocator* alloc, void* ptr) {
        static_cast<T*>(ptr)->~T();
        alloc->deallocate(ptr, sizeof(T), alignof(T));
    }

    type_id_t from_;
    type_id_t to_;
    type_id_t type_id_;
    void* payload_;
    MessageAllocator* allocator_;
    void (*destroy_fn_)(MessageAllocator*, void*);
};

template <typename T, typename... Args>
MessagePtr<T> make_message(Args&&... args) {
    auto& alloc = default_allocator();
    void* mem = alloc.allocate(sizeof(T), alignof(T));
    T* ptr = ::new (mem) T(std::forward<Args>(args)...);
    return MessagePtr<T>(ptr, &alloc);
}

class HeapAllocator : public MessageAllocator {
public:
    void* allocate(std::size_t size, std::size_t alignment) override;
    void deallocate(void* ptr, std::size_t size, std::size_t alignment) override;
    void lock() override;
    void unlock() override;

    static HeapAllocator& instance();
};

} // namespace cortexflow
