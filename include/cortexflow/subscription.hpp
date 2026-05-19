// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <utility>

namespace cortexflow {

// ---------------------------------------------------------------------------
// Subscription
// ---------------------------------------------------------------------------
//
// RAII handle for a cache subscription. Returned by `Cache::subscribe<K>(...)`.
// Holding one keeps a slot in the cache's subscription pool occupied; dropping
// it (or moving it to a destination that then drops) synchronously releases
// the slot back to the pool.
//
// Move-only by design (architecture §7.5). A moved-from handle is equivalent
// to a default-constructed one: its destructor is a no-op.
//
// The handle is intentionally untyped on the cache's template parameters so
// it can be stored generically (for example as a state-locals member when
// flow support lands — see PRD user story 25).
class Subscription {
public:
    // Trampoline signature used by Cache to release a slot by index without
    // exposing its template parameters at this header.
    using ReleaseFn = void (*)(void* ctx, std::size_t slot_index);

    Subscription() noexcept = default;

    Subscription(ReleaseFn release_fn, void* ctx, std::size_t index) noexcept
        : release_fn_(release_fn), ctx_(ctx), index_(index) {}

    ~Subscription() { release(); }

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept
        : release_fn_(other.release_fn_),
          ctx_(other.ctx_),
          index_(other.index_) {
        other.release_fn_ = nullptr;
    }

    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            release();
            release_fn_ = other.release_fn_;
            ctx_ = other.ctx_;
            index_ = other.index_;
            other.release_fn_ = nullptr;
        }
        return *this;
    }

    // True when this handle owns a live subscription slot.
    bool active() const noexcept { return release_fn_ != nullptr; }

    // Release the slot now. After this returns the handle is inert.
    void reset() noexcept { release(); }

private:
    void release() noexcept {
        if (release_fn_) {
            release_fn_(ctx_, index_);
            release_fn_ = nullptr;
        }
    }

    ReleaseFn release_fn_ = nullptr;
    void* ctx_ = nullptr;
    std::size_t index_ = 0;
};

} // namespace cortexflow
