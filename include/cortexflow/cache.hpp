// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdio>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <cortexflow/assert.hpp>
#include <cortexflow/messaging.hpp>
#include <cortexflow/subscription.hpp>
#include <cortexflow/type_name.hpp>

namespace cortexflow {

// ---------------------------------------------------------------------------
// Composition primitives owned by the cache subsystem
// ---------------------------------------------------------------------------

// Documentation marker: `Owned<K, M>` declares that module `M` is the writer
// of cache key `K`. In v1 this is parsed but not statically enforced — see
// PRD Out-of-Scope and architecture §7.4. The cache extracts the key type
// from `Owned<K, M>` wrappers transparently so plain keys and Owned-wrapped
// keys coexist in the same `CacheKeyList`.
template <typename Key, typename Owner>
struct Owned {
    using key_type = Key;
    using owner_type = Owner;
};

template <typename... Keys>
struct CacheKeyList {
    static constexpr std::size_t size = sizeof...(Keys);
};

// ---------------------------------------------------------------------------
// KeyChanged<K> — canonical notification payload (architecture §7.6)
// ---------------------------------------------------------------------------

template <typename K>
struct KeyChanged {
    using key_type = K;
    using value_type = typename K::value_type;

    // Empty on the first write (no prior value to compare against).
    std::optional<value_type> old_value;
    value_type new_value;
};

namespace detail {

// Strip the `Owned<K, _>` wrapper from a key list entry; pass plain keys
// through unchanged.
template <typename T>
struct key_of { using type = T; };

template <typename K, typename M>
struct key_of<Owned<K, M>> { using type = K; };

template <typename T>
using key_of_t = typename key_of<T>::type;

template <typename K>
struct Slot {
    std::optional<typename K::value_type> value;
};

template <typename List>
struct slots_tuple_for;

template <typename... Entries>
struct slots_tuple_for<CacheKeyList<Entries...>> {
    using type = std::tuple<Slot<key_of_t<Entries>>...>;
};

} // namespace detail

// ---------------------------------------------------------------------------
// Cache
// ---------------------------------------------------------------------------
//
// Typed key-value store integrated into the runtime. Reads return
// `std::optional<value_type>` and are empty until first write. Writes that
// compare equal to the prior value (per `operator==`) are silent; real changes
// post a `KeyChanged<K>` envelope to every subscriber for that key, in
// registration order, through the normal runtime queue.
//
// Subscriptions are managed through a fixed-size pool sized to
// `MaxSubscriptions`. `subscribe<K>(...)` returns a RAII `Subscription` whose
// destruction releases the slot synchronously. Overflow is a system-level
// `CORTEXFLOW_ASSERT` naming the pool capacity and the key type.
template <typename CacheKeyListT, std::size_t MaxSubscriptions>
class Cache {
public:
    using PostFn = void(*)(void*, Envelope&&);
    using key_list = CacheKeyListT;
    using slots_tuple_t = typename detail::slots_tuple_for<CacheKeyListT>::type;
    static constexpr std::size_t kMaxSubscriptions = MaxSubscriptions;

    Cache() = default;
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;
    Cache(Cache&&) = delete;
    Cache& operator=(Cache&&) = delete;

    void bind_post(PostFn fn, void* ctx) {
        post_fn_ = fn;
        post_ctx_ = ctx;
    }

    template <typename K>
    std::optional<typename K::value_type> get() const {
        return std::get<detail::Slot<K>>(slots_).value;
    }

    template <typename K>
    void set(typename K::value_type value) {
        auto& slot = std::get<detail::Slot<K>>(slots_);
        if (slot.value.has_value() && *slot.value == value) {
            return;
        }
        std::optional<typename K::value_type> old = slot.value;
        slot.value = std::move(value);
        fanout<K>(old, *slot.value);
    }

    template <typename K, typename Subscriber>
    [[nodiscard]] Subscription subscribe() {
        return subscribe<K>(type_id<Subscriber>());
    }

    template <typename K>
    [[nodiscard]] Subscription subscribe(type_id_t subscriber_id) {
        if (num_active_ >= kMaxSubscriptions) {
            overflow_fault<K>();
        }
        std::size_t slot_idx = find_free_slot();
        subs_[slot_idx].key_type_id = type_id<K>();
        subs_[slot_idx].subscriber_id = subscriber_id;
        subs_[slot_idx].active = true;
        order_[num_active_++] = slot_idx;
        return Subscription(&Cache::release_slot_trampoline, this, slot_idx);
    }

    std::size_t subscriber_count() const noexcept { return num_active_; }

private:
    template <typename K>
    [[noreturn]] void overflow_fault() const {
        // Format the failure reason once into a static buffer so the string
        // outlives the asserting frame; subscription overflow is a system
        // failure (architecture §13.1) so a single-slot buffer is safe.
        static char buf[192];
        constexpr auto kn = type_name<K>();
        std::snprintf(buf, sizeof(buf),
            "Cache subscription pool overflow (capacity=%zu, key=%.*s)",
            kMaxSubscriptions,
            static_cast<int>(kn.size()), kn.data());
        CORTEXFLOW_ASSERT(false, buf);
        // CORTEXFLOW_ASSERT(false, ...) is [[noreturn]] in practice; the
        // unreachable below silences any "function must return a value"
        // diagnostic in pathological build configurations.
        for (;;) {}
    }

    std::size_t find_free_slot() const {
        for (std::size_t i = 0; i < kMaxSubscriptions; ++i) {
            if (!subs_[i].active) {
                return i;
            }
        }
        // num_active_ < kMaxSubscriptions implies a free slot exists; reaching
        // here means the pool's internal accounting is corrupt.
        CORTEXFLOW_ASSERT(false,
            "Cache: no free subscription slot despite count < capacity");
        return 0;
    }

    static void release_slot_trampoline(void* ctx, std::size_t slot_idx) {
        static_cast<Cache*>(ctx)->release_slot(slot_idx);
    }

    void release_slot(std::size_t slot_idx) noexcept {
        CORTEXFLOW_ASSERT(slot_idx < kMaxSubscriptions,
            "Cache::release_slot: slot index out of range");
        CORTEXFLOW_ASSERT(subs_[slot_idx].active,
            "Cache::release_slot: slot already inactive");
        subs_[slot_idx].active = false;
        std::size_t pos = num_active_;
        for (std::size_t i = 0; i < num_active_; ++i) {
            if (order_[i] == slot_idx) {
                pos = i;
                break;
            }
        }
        CORTEXFLOW_ASSERT(pos < num_active_,
            "Cache::release_slot: slot not present in order list");
        for (std::size_t i = pos + 1; i < num_active_; ++i) {
            order_[i - 1] = order_[i];
        }
        --num_active_;
    }

    template <typename K>
    void fanout(const std::optional<typename K::value_type>& old_value,
                const typename K::value_type& new_value) {
        // Snapshot the active subscriber count at the start of the fanout so
        // any subscription created during the writer's handler (after `set`
        // returns but before the dispatcher hands out queued envelopes) does
        // not observe this write — architecture §7.5, "subscribe-during-write".
        const std::size_t snapshot = num_active_;
        const type_id_t k_id = type_id<K>();
        for (std::size_t i = 0; i < snapshot; ++i) {
            const std::size_t slot_idx = order_[i];
            if (subs_[slot_idx].key_type_id != k_id) {
                continue;
            }
            CORTEXFLOW_ASSERT(post_fn_ != nullptr,
                "Cache::set: post sink not bound");
            auto ptr = make_message<KeyChanged<K>>(
                KeyChanged<K>{old_value, new_value});
            Envelope env(kNoSender, subs_[slot_idx].subscriber_id,
                         std::move(ptr));
            post_fn_(post_ctx_, std::move(env));
        }
    }

    struct Subscriber {
        type_id_t key_type_id = 0;
        type_id_t subscriber_id = 0;
        bool active = false;
    };

    static constexpr std::size_t kPoolCapacity =
        MaxSubscriptions ? MaxSubscriptions : 1;

    slots_tuple_t slots_{};
    Subscriber subs_[kPoolCapacity]{};
    std::size_t order_[kPoolCapacity]{};
    std::size_t num_active_ = 0;
    PostFn post_fn_ = nullptr;
    void* post_ctx_ = nullptr;
};

} // namespace cortexflow
