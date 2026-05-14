#pragma once

#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <cortexflow/assert.hpp>
#include <cortexflow/messaging.hpp>
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
// Subscription registry is intentionally minimal — slice 10 will replace this
// with a RAII `Subscription` handle backed by a slot pool. The registration
// API here exists so this slice can be tested end-to-end via the queue.
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

    // Minimal registration. Slice 10 replaces this with a RAII `Subscription`
    // handle. `subscriber_id` is the `type_id<Subscriber>()` of the receiving
    // module; the envelope's `to` field is set to this id and dispatch routes
    // through the normal queue.
    template <typename K, typename Subscriber>
    void subscribe() {
        subscribe<K>(type_id<Subscriber>());
    }

    template <typename K>
    void subscribe(type_id_t subscriber_id) {
        CORTEXFLOW_ASSERT(num_subs_ < kMaxSubscriptions,
            "Cache subscription pool overflow");
        subs_[num_subs_].key_type_id = type_id<K>();
        subs_[num_subs_].subscriber_id = subscriber_id;
        ++num_subs_;
    }

    std::size_t subscriber_count() const noexcept { return num_subs_; }

private:
    template <typename K>
    void fanout(const std::optional<typename K::value_type>& old_value,
                const typename K::value_type& new_value) {
        // Snapshot subscriber count: any subscription added during this call
        // (which cannot happen via user code, since fanout only enqueues
        // envelopes and does not dispatch handlers) does not see this write.
        const std::size_t snapshot = num_subs_;
        const type_id_t k_id = type_id<K>();
        for (std::size_t i = 0; i < snapshot; ++i) {
            if (subs_[i].key_type_id != k_id) {
                continue;
            }
            CORTEXFLOW_ASSERT(post_fn_ != nullptr,
                "Cache::set: post sink not bound");
            auto ptr = make_message<KeyChanged<K>>(
                KeyChanged<K>{old_value, new_value});
            Envelope env(kNoSender, subs_[i].subscriber_id, std::move(ptr));
            post_fn_(post_ctx_, std::move(env));
        }
    }

    struct Subscriber {
        type_id_t key_type_id = 0;
        type_id_t subscriber_id = 0;
    };

    slots_tuple_t slots_{};
    Subscriber subs_[MaxSubscriptions ? MaxSubscriptions : 1]{};
    std::size_t num_subs_ = 0;
    PostFn post_fn_ = nullptr;
    void* post_ctx_ = nullptr;
};

} // namespace cortexflow
