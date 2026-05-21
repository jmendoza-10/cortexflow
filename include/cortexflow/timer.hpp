// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include <cortexflow/assert.hpp>
#include <cortexflow/clock.hpp>
#include <cortexflow/messaging.hpp>
#include <cortexflow/trace.hpp>
#include <cortexflow/type_name.hpp>

namespace cortexflow {

// ---------------------------------------------------------------------------
// Timer
// ---------------------------------------------------------------------------
//
// RAII handle for an armed timer. Returned by `TimerService::arm(...)`. While
// the handle is live, the underlying timer occupies a heap entry in the
// service; dropping the handle (or moving it to a destination that then
// drops) cancels the timer synchronously — its envelope is not posted.
//
// Move-only. A moved-from handle is equivalent to a default-constructed one:
// its destructor is a no-op.
//
// The handle is intentionally untyped on the message/target types so it can
// be stored generically (for example as a state-locals member when flow
// support lands — PRD user story 40).
class Timer {
public:
    // Trampoline signature used by TimerService to mark a heap entry as
    // cancelled by sequence id without exposing its internals here.
    using CancelFn = void (*)(void* ctx, std::size_t seq);

    Timer() noexcept = default;

    Timer(CancelFn fn, void* ctx, std::size_t seq) noexcept
        : cancel_fn_(fn), ctx_(ctx), seq_(seq) {}

    ~Timer() { release(); }

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    Timer(Timer&& other) noexcept
        : cancel_fn_(other.cancel_fn_), ctx_(other.ctx_), seq_(other.seq_) {
        other.cancel_fn_ = nullptr;
    }

    Timer& operator=(Timer&& other) noexcept {
        if (this != &other) {
            release();
            cancel_fn_ = other.cancel_fn_;
            ctx_ = other.ctx_;
            seq_ = other.seq_;
            other.cancel_fn_ = nullptr;
        }
        return *this;
    }

    bool active() const noexcept { return cancel_fn_ != nullptr; }

    // Cancel now. After this returns the handle is inert.
    void cancel() noexcept { release(); }

private:
    void release() noexcept {
        if (cancel_fn_) {
            cancel_fn_(ctx_, seq_);
            cancel_fn_ = nullptr;
        }
    }

    CancelFn cancel_fn_ = nullptr;
    void* ctx_ = nullptr;
    std::size_t seq_ = 0;
};

// ---------------------------------------------------------------------------
// TimerService
// ---------------------------------------------------------------------------
//
// Runtime-level facility (not a module) accessed via `runtime.timers()`.
// `arm(...)` registers an envelope to be posted into the runtime's queue once
// a duration has elapsed on the bound clock. There are no synchronous
// callbacks; the expiry effect is dispatched on the same thread as every
// other handler.
//
// Host backend (this implementation): a min-heap of due times keyed off
// `Clock::now()`. `ManualClock::advance(duration)` invokes
// `TimerService::fire_due()` via the advance handler hook, walking the heap
// and posting every envelope whose due time has arrived. Timers armed during
// the firing of others — i.e. armed by handlers dispatched after `fire_due`
// returns — are NOT processed in the same advance window because `fire_due`
// holds its mutex across the entire heap walk and posts only what was in the
// heap when the lock was acquired.
//
// Production `SteadyClock` provides no advance event of its own, but the host
// `Runtime::run` loop pumps `fire_due()` on every wake of its bounded
// `cv_.wait_until` (see issue 20) so wall-clock-armed timers fire on schedule
// without a dedicated ticker thread. Other targets (FreeRTOS, bare metal)
// will register their own fire path on the matching `Clock` subclass.
//
// Concurrency: every public method is safe to call from any thread (the loop
// thread or a foreign boundary-module thread). One mutex serialises the heap
// and the live-seq set.
class TimerService {
public:
    using PostFn = void (*)(void* ctx, Envelope&& env);

    explicit TimerService(Clock& clock) noexcept : clock_(&clock) {
        clock_->install_advance_handler(&TimerService::fire_due_trampoline,
                                        this);
    }

    ~TimerService() = default;

    TimerService(const TimerService&) = delete;
    TimerService& operator=(const TimerService&) = delete;
    TimerService(TimerService&&) = delete;
    TimerService& operator=(TimerService&&) = delete;

    // Install the post sink used when timers fire. The runtime binds this in
    // `start()` before any module's `on_start()` runs, so handlers may freely
    // arm timers from `on_start`.
    void bind_post(PostFn fn, void* ctx) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        post_fn_ = fn;
        post_ctx_ = ctx;
    }

    // Generic, non-templated arm. Stores `env` until `delay` elapses on the
    // bound clock (envelope is posted via the bound sink) or the returned
    // `Timer` handle is dropped (envelope is destroyed without posting).
    //
    // Optional `owner_name` / `type_name` are routed into the FULL-level
    // trace at arm / cancel / fire and default to `"-"` when the caller
    // has no static information to surface — the templated `arm<Target,
    // Msg>` overload below threads concrete type names from the call
    // site so most arms render with real identifiers.
    [[nodiscard]] Timer arm(Clock::duration delay, Envelope env,
                            const char* owner_name = "-",
                            const char* type_name = "-") {
        CORTEXFLOW_ASSERT(delay.count() >= 0,
            "TimerService::arm called with negative duration");
        std::size_t seq;
        long long due_ms;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto due = clock_->now() + delay;
            seq = next_seq_++;
            live_seqs_.insert(seq);
            heap_.push_back(
                Entry{due, seq, std::move(env), owner_name, type_name});
            std::push_heap(heap_.begin(), heap_.end(), Entry::Cmp{});
            due_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         delay).count();
        }
        char field_buf[40];
        std::snprintf(field_buf, sizeof(field_buf), "due_ms=%lld", due_ms);
        CORTEXFLOW_TRACE_FULL(
            "timer_arm", owner_name, "-", type_name, field_buf);
        return Timer(&TimerService::cancel_trampoline, this, seq);
    }

    // Convenience: build a `kNoSender → Target` envelope and arm it. The
    // matching `Module::send<>` machinery enforces that `Target` declares a
    // handler for `Msg`; this lower-level path does not, by design — the
    // timer service is a runtime-level facility usable from contexts that
    // don't go through `send`.
    //
    // The trace owner_name is taken as the timer's `Target` (the message
    // destination) since callers in practice arm timers that deliver
    // back to themselves — the target is the conceptual owner of the
    // armed timer's effect.
    template <typename Target, typename Msg>
    [[nodiscard]] Timer arm(Clock::duration delay, Msg&& msg) {
        auto ptr = make_message<std::decay_t<Msg>>(std::forward<Msg>(msg));
        Envelope env(kNoSender, type_id<Target>(), std::move(ptr));
        return arm(delay, std::move(env),
                   type_name_cstr<Target>(),
                   type_name_cstr<std::decay_t<Msg>>());
    }

    // Drain every timer whose due time is ≤ `clock.now()`, posting each
    // expired envelope through the bound sink. The heap snapshot is taken
    // under the mutex so timers armed during firing are NOT processed in
    // this call (architecture §9, advance-window semantics).
    void fire_due() {
        // Collect under the lock; post after releasing so the runtime's own
        // queue mutex cannot deadlock against ours and so a foreign producer
        // calling `arm()` cannot stall on this thread's `post` call chain.
        struct FiringRecord {
            Envelope envelope;
            const char* owner_name;
            const char* type_name;
        };
        std::vector<FiringRecord> to_fire;
        PostFn fn = nullptr;
        void* ctx = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            fn = post_fn_;
            ctx = post_ctx_;
            const auto now = clock_->now();
            while (!heap_.empty() && heap_.front().due <= now) {
                std::pop_heap(heap_.begin(), heap_.end(), Entry::Cmp{});
                Entry entry = std::move(heap_.back());
                heap_.pop_back();
                auto it = live_seqs_.find(entry.seq);
                if (it == live_seqs_.end()) {
                    continue;  // cancelled before firing — entry is dropped
                }
                live_seqs_.erase(it);
                to_fire.push_back(FiringRecord{
                    std::move(entry.envelope),
                    entry.owner_name, entry.type_name});
            }
        }
        if (to_fire.empty()) {
            return;
        }
        CORTEXFLOW_ASSERT(fn != nullptr,
            "TimerService: post sink not bound when firing timers");
        for (auto& rec : to_fire) {
            // Emit the FULL-level fire trace *before* posting so the
            // ordering in the trace stream matches "timer fired, then
            // its envelope arrived at the recipient" — a reader can
            // verify causality without consulting timestamps.
            CORTEXFLOW_TRACE_FULL(
                "timer_fire", rec.owner_name, "-", rec.type_name, "");
            fn(ctx, std::move(rec.envelope));
        }
    }

    // Drop every armed timer and unbind the post sink. Called by the runtime
    // on `shutdown()` after modules destruct (their state-local Timer dtors
    // mark seqs cancelled, but the heap entries themselves are reclaimed
    // here).
    void clear() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        heap_.clear();
        live_seqs_.clear();
        post_fn_ = nullptr;
        post_ctx_ = nullptr;
    }

    // Test introspection: number of currently-live (not-yet-fired,
    // not-cancelled) timers. Cancelled entries still occupying the heap until
    // a future `fire_due` reaps them are excluded from this count.
    std::size_t armed_count() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return live_seqs_.size();
    }

    // Due time of the earliest live timer, expressed as a `Clock::duration`
    // since the bound clock's epoch (matching `Clock::now()`). The host
    // runtime uses this to bound the `cv_.wait_until` deadline in `run()` so
    // wall-clock-armed timers fire on schedule without a separate ticker
    // thread. Returns `std::nullopt` when no live timer is armed (the heap
    // may still contain lazily-cancelled tombstones; those are skipped).
    //
    // Implementation: a linear scan of the heap, picking the minimum due
    // among entries whose `seq` is still in `live_seqs_`. The heap top alone
    // is not sufficient because the earliest entry may have been cancelled
    // and will be reaped only on the next `fire_due` pop — using its due
    // time would over-wake the run loop without anything to fire.
    std::optional<Clock::duration> next_due_at() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        std::optional<Clock::duration> earliest;
        for (const auto& e : heap_) {
            if (live_seqs_.find(e.seq) == live_seqs_.end()) {
                continue;
            }
            if (!earliest || e.due < *earliest) {
                earliest = e.due;
            }
        }
        return earliest;
    }

private:
    struct Entry {
        Clock::duration due{};
        std::size_t seq = 0;
        Envelope envelope;
        // Names captured at arm time for the FULL-level trace at fire /
        // cancel. Both point into static storage (either the per-type
        // buffer from `type_name_cstr<>` or the `"-"` string literal),
        // so the pointers stay live for the lifetime of the entry.
        const char* owner_name = "-";
        const char* type_name = "-";

        struct Cmp {
            bool operator()(const Entry& a, const Entry& b) const noexcept {
                return a.due > b.due;  // min-heap: earlier due time at top
            }
        };
    };

    static void cancel_trampoline(void* ctx, std::size_t seq) noexcept {
        static_cast<TimerService*>(ctx)->cancel_by_seq(seq);
    }

    static void fire_due_trampoline(void* ctx) {
        static_cast<TimerService*>(ctx)->fire_due();
    }

    void cancel_by_seq(std::size_t seq) noexcept {
        const char* owner_name = nullptr;
        const char* type_name = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Lazy cancel: removing the seq from `live_seqs_` is the
            // cancellation signal — `fire_due` skips any popped entry
            // whose seq is not in the set. Calling cancel after the
            // timer fired (when the seq is no longer live) is a no-op,
            // which keeps Timer destruction safe even post-expiry.
            auto it = live_seqs_.find(seq);
            if (it == live_seqs_.end()) {
                return;  // already fired / cancelled
            }
            live_seqs_.erase(it);
            // Recover the names captured at arm time. The heap is small
            // (bounded by armed_count) so a linear scan under the
            // already-held mutex is the simplest path; this stays off
            // the hot dispatch path.
            for (const auto& e : heap_) {
                if (e.seq == seq) {
                    owner_name = e.owner_name;
                    type_name = e.type_name;
                    break;
                }
            }
        }
        // Emit outside the lock to keep trace_emit's I/O off the timer
        // service mutex. Strings remain valid (static storage).
        if (owner_name != nullptr) {
            CORTEXFLOW_TRACE_FULL(
                "timer_cancel", owner_name, "-", type_name, "");
        }
    }

    Clock* clock_;
    mutable std::mutex mutex_;
    PostFn post_fn_ = nullptr;
    void* post_ctx_ = nullptr;
    std::vector<Entry> heap_;
    std::unordered_set<std::size_t> live_seqs_;
    std::size_t next_seq_ = 1;
};

} // namespace cortexflow
