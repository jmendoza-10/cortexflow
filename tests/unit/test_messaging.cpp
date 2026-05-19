// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/messaging.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// --- Test message types ---

struct SimpleMsg {
    int value;
};

struct MoveOnlyMsg {
    std::unique_ptr<int> data;

    explicit MoveOnlyMsg(int v) : data(new int(v)) {}
    MoveOnlyMsg(MoveOnlyMsg&&) = default;
    MoveOnlyMsg& operator=(MoveOnlyMsg&&) = default;
    MoveOnlyMsg(const MoveOnlyMsg&) = delete;
    MoveOnlyMsg& operator=(const MoveOnlyMsg&) = delete;
};

struct OtherMsg {
    float x;
};

// --- Tracking allocator for verifying routing ---

class TrackingAllocator : public cortexflow::MessageAllocator {
public:
    int alloc_count = 0;
    int dealloc_count = 0;
    bool locked = false;
    int max_alloc_with_lock_held = 0;
    int alloc_with_lock_held = 0;
    int dealloc_with_lock_held = 0;
    int lock_count = 0;
    int unlock_count = 0;

    void* allocate(std::size_t size, std::size_t alignment) override {
        ++alloc_count;
        if (locked) ++alloc_with_lock_held;
        return ::operator new(size, std::align_val_t{alignment}, std::nothrow);
    }

    void deallocate(void* ptr, std::size_t /*size*/,
                    std::size_t alignment) override {
        ++dealloc_count;
        if (locked) ++dealloc_with_lock_held;
        ::operator delete(ptr, std::align_val_t{alignment});
    }

    void lock() override {
        locked = true;
        ++lock_count;
    }
    void unlock() override {
        locked = false;
        ++unlock_count;
    }
};

using cortexflow::Envelope;
using cortexflow::HeapAllocator;
using cortexflow::kNoSender;
using cortexflow::make_message;
using cortexflow::MessageAllocator;
using cortexflow::MessagePtr;
using cortexflow::type_id;

// ---------------------------------------------------------------------------
// MessagePtr
// ---------------------------------------------------------------------------

TEST_CASE("MessagePtr default-constructs to null") {
    MessagePtr<SimpleMsg> ptr;
    CHECK(!ptr);
    CHECK(ptr.get() == nullptr);
}

TEST_CASE("make_message constructs a valid MessagePtr") {
    auto msg = make_message<SimpleMsg>(SimpleMsg{42});
    CHECK(msg);
    CHECK(msg->value == 42);
}

TEST_CASE("MessagePtr is move-constructible") {
    auto msg = make_message<SimpleMsg>(SimpleMsg{7});
    auto moved = std::move(msg);
    CHECK(moved);
    CHECK(moved->value == 7);
    CHECK(!msg);
}

TEST_CASE("MessagePtr is move-assignable") {
    auto a = make_message<SimpleMsg>(SimpleMsg{1});
    auto b = make_message<SimpleMsg>(SimpleMsg{2});
    a = std::move(b);
    CHECK(a);
    CHECK(a->value == 2);
    CHECK(!b);
}

TEST_CASE("MessagePtr routes deallocation through originating allocator") {
    TrackingAllocator alloc;
    {
        void* mem = alloc.allocate(sizeof(SimpleMsg), alignof(SimpleMsg));
        auto* raw = ::new (mem) SimpleMsg{99};
        MessagePtr<SimpleMsg> msg(raw, &alloc);
        CHECK(alloc.alloc_count == 1);
        CHECK(alloc.dealloc_count == 0);
    }
    CHECK(alloc.dealloc_count == 1);
}

TEST_CASE("MessagePtr supports move-only payloads") {
    auto msg = make_message<MoveOnlyMsg>(42);
    CHECK(msg);
    CHECK(*msg->data == 42);

    auto moved = std::move(msg);
    CHECK(moved);
    CHECK(*moved->data == 42);
    CHECK(!msg);
}

// ---------------------------------------------------------------------------
// MessagePtr — static checks
// ---------------------------------------------------------------------------

TEST_CASE("MessagePtr is not copyable (static_assert)") {
    static_assert(!std::is_copy_constructible<MessagePtr<SimpleMsg>>::value);
    static_assert(!std::is_copy_assignable<MessagePtr<SimpleMsg>>::value);
    CHECK(true);
}

// ---------------------------------------------------------------------------
// Envelope
// ---------------------------------------------------------------------------

TEST_CASE("Envelope carries from, to, and type_id") {
    constexpr auto to_id = type_id<SimpleMsg>();
    auto msg = make_message<SimpleMsg>(SimpleMsg{10});
    Envelope env(kNoSender, to_id, std::move(msg));

    CHECK(env.from() == kNoSender);
    CHECK(env.to() == to_id);
    CHECK(env.payload_type_id() == type_id<SimpleMsg>());
}

TEST_CASE("Envelope sentinel from (kNoSender)") {
    auto msg = make_message<SimpleMsg>(SimpleMsg{0});
    Envelope env(kNoSender, type_id<SimpleMsg>(), std::move(msg));
    CHECK(env.from() == kNoSender);
    CHECK(env.from() == 0);
}

TEST_CASE("Envelope non-sentinel from") {
    constexpr auto sender = type_id<OtherMsg>();
    auto msg = make_message<SimpleMsg>(SimpleMsg{5});
    Envelope env(sender, type_id<SimpleMsg>(), std::move(msg));
    CHECK(env.from() == sender);
    CHECK(env.from() != kNoSender);
}

TEST_CASE("Envelope payload round-trip") {
    auto msg = make_message<SimpleMsg>(SimpleMsg{123});
    Envelope env(kNoSender, type_id<SimpleMsg>(), std::move(msg));
    CHECK(env.payload<SimpleMsg>().value == 123);
}

TEST_CASE("Envelope move-only payload round-trip") {
    auto msg = make_message<MoveOnlyMsg>(77);
    Envelope env(kNoSender, type_id<MoveOnlyMsg>(), std::move(msg));

    auto& payload = env.payload<MoveOnlyMsg>();
    CHECK(payload.data != nullptr);
    CHECK(*payload.data == 77);
}

TEST_CASE("Envelope is move-constructible") {
    auto msg = make_message<SimpleMsg>(SimpleMsg{33});
    Envelope env(kNoSender, type_id<SimpleMsg>(), std::move(msg));
    Envelope moved(std::move(env));

    CHECK(moved.payload<SimpleMsg>().value == 33);
    CHECK(moved.from() == kNoSender);
}

TEST_CASE("Envelope is move-assignable") {
    auto msg1 = make_message<SimpleMsg>(SimpleMsg{1});
    auto msg2 = make_message<SimpleMsg>(SimpleMsg{2});
    Envelope a(kNoSender, type_id<SimpleMsg>(), std::move(msg1));
    Envelope b(kNoSender, type_id<SimpleMsg>(), std::move(msg2));

    a = std::move(b);
    CHECK(a.payload<SimpleMsg>().value == 2);
}

TEST_CASE("Envelopes with same message type have equal type_id") {
    auto msg1 = make_message<SimpleMsg>(SimpleMsg{1});
    auto msg2 = make_message<SimpleMsg>(SimpleMsg{2});
    Envelope a(kNoSender, type_id<SimpleMsg>(), std::move(msg1));
    Envelope b(kNoSender, type_id<SimpleMsg>(), std::move(msg2));

    CHECK(a.payload_type_id() == b.payload_type_id());
}

TEST_CASE("Envelopes with different message types have different type_id") {
    auto msg1 = make_message<SimpleMsg>(SimpleMsg{1});
    auto msg2 = make_message<OtherMsg>(OtherMsg{2.0f});
    Envelope a(kNoSender, type_id<SimpleMsg>(), std::move(msg1));
    Envelope b(kNoSender, type_id<OtherMsg>(), std::move(msg2));

    CHECK(a.payload_type_id() != b.payload_type_id());
}

// ---------------------------------------------------------------------------
// Envelope — static checks
// ---------------------------------------------------------------------------

TEST_CASE("Envelope is not copyable (static_assert)") {
    static_assert(!std::is_copy_constructible<Envelope>::value);
    static_assert(!std::is_copy_assignable<Envelope>::value);
    CHECK(true);
}

// ---------------------------------------------------------------------------
// Envelope RAII — destruction routes through allocator
// ---------------------------------------------------------------------------

TEST_CASE("Envelope destruction deallocates through originating allocator") {
    TrackingAllocator alloc;
    {
        void* mem = alloc.allocate(sizeof(SimpleMsg), alignof(SimpleMsg));
        auto* raw = ::new (mem) SimpleMsg{0};
        MessagePtr<SimpleMsg> msg(raw, &alloc);
        Envelope env(kNoSender, type_id<SimpleMsg>(), std::move(msg));
        CHECK(alloc.alloc_count == 1);
        CHECK(alloc.dealloc_count == 0);
    }
    CHECK(alloc.dealloc_count == 1);
}

TEST_CASE("Envelope move transfers allocator ownership") {
    TrackingAllocator alloc;
    {
        void* mem = alloc.allocate(sizeof(SimpleMsg), alignof(SimpleMsg));
        auto* raw = ::new (mem) SimpleMsg{0};
        MessagePtr<SimpleMsg> msg(raw, &alloc);
        Envelope env(kNoSender, type_id<SimpleMsg>(), std::move(msg));

        Envelope moved(std::move(env));
        CHECK(alloc.dealloc_count == 0);
    }
    CHECK(alloc.dealloc_count == 1);
}

// ---------------------------------------------------------------------------
// Allocator interface
// ---------------------------------------------------------------------------

TEST_CASE("HeapAllocator is a MessageAllocator") {
    static_assert(std::is_base_of<MessageAllocator, HeapAllocator>::value);
    CHECK(true);
}

TEST_CASE("HeapAllocator lock/unlock") {
    auto& alloc = HeapAllocator::instance();
    alloc.lock();
    alloc.unlock();
    CHECK(true);
}

TEST_CASE("default_allocator returns HeapAllocator instance") {
    auto& alloc = cortexflow::default_allocator();
    CHECK(&alloc == &HeapAllocator::instance());
}

// ---------------------------------------------------------------------------
// AllocatorLock RAII
// ---------------------------------------------------------------------------

TEST_CASE("AllocatorLock acquires on construction and releases on destruction") {
    TrackingAllocator alloc;
    {
        cortexflow::AllocatorLock guard(alloc);
        CHECK(alloc.locked);
        CHECK(alloc.lock_count == 1);
        CHECK(alloc.unlock_count == 0);
    }
    CHECK_FALSE(alloc.locked);
    CHECK(alloc.unlock_count == 1);
}

// ---------------------------------------------------------------------------
// Foreign-thread allocator path
// ---------------------------------------------------------------------------

TEST_CASE("make_message_with holds allocator lock during allocate") {
    TrackingAllocator alloc;
    auto msg = cortexflow::make_message_with<SimpleMsg>(alloc, SimpleMsg{17});

    CHECK(msg);
    CHECK(msg->value == 17);
    CHECK(alloc.alloc_count == 1);
    CHECK(alloc.alloc_with_lock_held == 1);
    CHECK(alloc.lock_count == 1);
    CHECK(alloc.unlock_count == 1);
    CHECK_FALSE(alloc.locked);
}

TEST_CASE("MessagePtr destruction holds allocator lock during deallocate") {
    TrackingAllocator alloc;
    {
        auto msg = cortexflow::make_message_with<SimpleMsg>(alloc, SimpleMsg{1});
        CHECK(alloc.lock_count == 1);
    }
    CHECK(alloc.dealloc_count == 1);
    CHECK(alloc.dealloc_with_lock_held == 1);
    CHECK(alloc.lock_count == 2);
    CHECK(alloc.unlock_count == 2);
}

TEST_CASE("Envelope destruction holds allocator lock during deallocate") {
    TrackingAllocator alloc;
    {
        auto msg = cortexflow::make_message_with<SimpleMsg>(alloc, SimpleMsg{2});
        Envelope env(kNoSender, type_id<SimpleMsg>(), std::move(msg));
        CHECK(alloc.lock_count == 1);
    }
    CHECK(alloc.dealloc_count == 1);
    CHECK(alloc.dealloc_with_lock_held == 1);
    CHECK(alloc.lock_count == 2);
}

TEST_CASE(
    "make_message_with from a foreign thread holds the allocator lock") {
    TrackingAllocator alloc;
    std::atomic<bool> observed_locked{false};
    std::atomic<bool> observed_unlocked{false};

    std::thread foreign([&] {
        for (int i = 0; i < 64; ++i) {
            auto msg = cortexflow::make_message_with<SimpleMsg>(
                alloc, SimpleMsg{i});
            (void)msg;  // destructor releases through the allocator
        }
        // After construction returns, the lock is released.
        observed_unlocked.store(!alloc.locked);
        observed_locked.store(alloc.lock_count > 0);
    });
    foreign.join();

    CHECK(observed_locked.load());
    CHECK(observed_unlocked.load());
    CHECK(alloc.alloc_count == 64);
    CHECK(alloc.dealloc_count == 64);
    // Every allocate AND deallocate happened with the lock held.
    CHECK(alloc.alloc_with_lock_held == 64);
    CHECK(alloc.dealloc_with_lock_held == 64);
    CHECK(alloc.lock_count == 128);
    CHECK(alloc.unlock_count == 128);
}
