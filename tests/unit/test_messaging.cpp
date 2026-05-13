#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/messaging.hpp>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

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

    void* allocate(std::size_t size, std::size_t alignment) override {
        ++alloc_count;
        return ::operator new(size, std::align_val_t{alignment}, std::nothrow);
    }

    void deallocate(void* ptr, std::size_t /*size*/,
                    std::size_t alignment) override {
        ++dealloc_count;
        ::operator delete(ptr, std::align_val_t{alignment});
    }

    void lock() override { locked = true; }
    void unlock() override { locked = false; }
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
