// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <cortexflow/assert.hpp>
#include <cortexflow/cache.hpp>
#include <cortexflow/clock.hpp>
#include <cortexflow/messaging.hpp>
#include <cortexflow/module.hpp>
#include <cortexflow/timer.hpp>
#include <cortexflow/trace.hpp>
#include <cortexflow/type_name.hpp>

namespace cortexflow {

// ---------------------------------------------------------------------------
// Composition primitives
// ---------------------------------------------------------------------------

template <typename... Modules>
struct ModuleList {
    using tuple_type = std::tuple<Modules...>;
    static constexpr std::size_t size = sizeof...(Modules);
};

// `Owned<K, M>` and `CacheKeyList<...>` live in cache.hpp.

template <std::size_t N>
struct MaxSubscriptions {
    static constexpr std::size_t kMaxSubscriptions = N;
};

template <std::size_t N>
struct DrainBudget {
    static constexpr std::size_t kDrainBudget = N;
};

namespace detail {

// Composition validator: detect a duplicate module type in any variadic
// template list (matches ModuleList<Modules...>). Used by the Runtime-level
// `static_assert` below. Per-pair traits used by Module::send live in
// module.hpp so the send call site does not need to include runtime.hpp.
template <typename...>
struct pack_has_duplicates : std::false_type {};

template <typename First, typename... Rest>
struct pack_has_duplicates<First, Rest...>
    : std::bool_constant<
          std::disjunction_v<std::is_same<First, Rest>...> ||
          pack_has_duplicates<Rest...>::value> {};

template <typename>
struct list_has_duplicates : std::false_type {};

template <template <typename...> class List, typename... Ts>
struct list_has_duplicates<List<Ts...>> : pack_has_duplicates<Ts...> {};

template <typename List>
inline constexpr bool list_has_duplicates_v = list_has_duplicates<List>::value;

// Per-option SFINAE probes — produce { present, value } for each option kind.
template <typename T, typename = void>
struct drain_budget_of {
    static constexpr bool present = false;
    static constexpr std::size_t value = 0;
};
template <typename T>
struct drain_budget_of<T, std::void_t<decltype(T::kDrainBudget)>> {
    static constexpr bool present = true;
    static constexpr std::size_t value = T::kDrainBudget;
};

template <typename T, typename = void>
struct max_subs_of {
    static constexpr bool present = false;
    static constexpr std::size_t value = 0;
};
template <typename T>
struct max_subs_of<T, std::void_t<decltype(T::kMaxSubscriptions)>> {
    static constexpr bool present = true;
    static constexpr std::size_t value = T::kMaxSubscriptions;
};

// Linear search over the option pack: first match wins.
template <template <typename, typename> class Probe,
          std::size_t Default, typename... Opts>
struct find_option {
    static constexpr std::size_t value = Default;
};

template <template <typename, typename> class Probe,
          std::size_t Default, typename First, typename... Rest>
struct find_option<Probe, Default, First, Rest...> {
    static constexpr std::size_t value =
        Probe<First, void>::present
            ? Probe<First, void>::value
            : find_option<Probe, Default, Rest...>::value;
};

} // namespace detail

template <typename... Opts>
struct Config {
    static constexpr std::size_t kDrainBudget =
        detail::find_option<detail::drain_budget_of,
                            static_cast<std::size_t>(-1), Opts...>::value;
    static constexpr std::size_t kMaxSubscriptions =
        detail::find_option<detail::max_subs_of, 16, Opts...>::value;
};

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------

template <typename ModuleListT, typename CacheKeyListT, typename ConfigT>
class Runtime {
    // Composition validator. Fires at the point a user instantiates the
    // Runtime, which is the front door for mis-wirings. Message wording is
    // user-facing — the offending type appears in the surrounding template
    // instantiation note that the compiler prints alongside this assertion.
    static_assert(!detail::list_has_duplicates_v<ModuleListT>,
        "cortexflow::Runtime: duplicate module type declared in ModuleList "
        "(each module type may appear at most once)");

public:
    using modules_tuple = typename ModuleListT::tuple_type;
    using cache_type = Cache<CacheKeyListT, ConfigT::kMaxSubscriptions>;
    static constexpr std::size_t kNumModules =
        std::tuple_size_v<modules_tuple>;
    static constexpr std::size_t kDrainBudget = ConfigT::kDrainBudget;
    static constexpr std::size_t kMaxSubscriptions =
        ConfigT::kMaxSubscriptions;

    Runtime() : clock_(&default_steady_clock()), timers_(*clock_) {}
    explicit Runtime(Clock& clock) : clock_(&clock), timers_(clock) {}
    ~Runtime() {
        // If user forgot to shutdown(), modules destruct via tuple destructor
        // (reverse declaration order). No on_stop() runs in that path — the
        // user is responsible for orderly teardown.
    }

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;

    // Phase 1: construct all modules (declaration order, by tuple semantics).
    // Phase 2: bind post sinks, then on_start() in declaration order.
    //
    // The cache is constructed before modules so module `on_start()` handlers
    // may freely subscribe to keys and seed initial values.
    void start() {
        CORTEXFLOW_ASSERT(!started_, "Runtime::start called twice");
        cache_.emplace();
        cache_->bind_post(&post_trampoline, this);
        timers_.bind_post(&post_trampoline, this);
        modules_.emplace();
        bind_each(*modules_,
                  std::make_index_sequence<kNumModules>{});
        on_start_each(*modules_,
                      std::make_index_sequence<kNumModules>{});
        started_ = true;
    }

    // Drain the queue fully and return. Returns immediately if empty.
    // Messages posted during dispatch are processed in the same call.
    void run_one() {
        CORTEXFLOW_ASSERT(started_, "Runtime::run_one before start()");
        while (true) {
            std::optional<Envelope> env;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (queue_.empty()) {
                    return;
                }
                env.emplace(std::move(queue_.front()));
                queue_.pop_front();
            }
            dispatch(*env);
        }
    }

    // Block on CV, drain, repeat. Returns when stop() is signaled and
    // the queue has been drained.
    //
    // The CV wait is bounded so wall-clock-armed timers fire on schedule
    // under a real-time clock (production `SteadyClock`) without a separate
    // ticker thread: every iteration computes a deadline that is the earlier
    // of `next_due_at()` (when a timer is armed) and `min_tick_interval`
    // (the heartbeat that keeps the loop responsive to `stop()` and timers
    // armed from inside a handler). After each wake the loop calls
    // `timers_.fire_due()` against the current clock; under `ManualClock`
    // this is a no-op until tests call `clock.advance(...)`, preserving
    // test-only determinism semantics. See issue 20.
    void run() {
        CORTEXFLOW_ASSERT(started_, "Runtime::run before start()");
        while (true) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                const auto deadline = std::chrono::steady_clock::now() +
                                      compute_wait_budget();
                cv_.wait_until(lock, deadline, [&] {
                    return !queue_.empty() || stop_requested_.load();
                });
                if (queue_.empty() && stop_requested_.load()) {
                    return;
                }
            }
            // Fire any wall-clock-due timers before draining the queue so
            // their envelopes interleave with normal posts in the same
            // dispatch pass. Cheap when nothing is due — the service tests
            // its heap under its own mutex and returns early.
            timers_.fire_due();
            run_one();
            if (stop_requested_.load()) {
                // Drain any final messages posted during the last batch.
                run_one();
                return;
            }
        }
    }

    // Signal the loop to exit. Thread-safe; safe to call from any thread.
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_requested_ = true;
        }
        cv_.notify_all();
    }

    // Bounded drain → on_stop() reverse → destruct reverse. The drain is
    // bounded by ConfigT::kDrainBudget; exhaustion logs WARN and aborts the
    // remaining drain without asserting.
    void shutdown() {
        CORTEXFLOW_ASSERT(started_, "Runtime::shutdown before start()");
        std::size_t drained = 0;
        while (drained < kDrainBudget) {
            std::optional<Envelope> env;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (queue_.empty()) {
                    break;
                }
                env.emplace(std::move(queue_.front()));
                queue_.pop_front();
            }
            dispatch(*env);
            ++drained;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!queue_.empty()) {
                CORTEXFLOW_TRACE_WARN(
                    "drain", "-", "-", "-",
                    "drain budget exhausted; remaining envelopes discarded");
                queue_.clear();
            }
        }
        on_stop_reverse(*modules_,
                        std::make_index_sequence<kNumModules>{});
        modules_.reset();
        cache_.reset();
        timers_.clear();
        started_ = false;
        stop_requested_ = false;
    }

    // Thread-safe enqueue. Callable from any thread (including foreign
    // boundary-module threads) and from inside a handler running on the loop
    // thread. Synchronisation: the queue mutex serialises producers; a
    // foreign-thread post wakes a `run()` blocked in the CV via notify_one.
    //
    // Wake-on-stop: posts arriving after `stop()` has been observed are
    // silently dropped. Boundary-module threads commonly race with `stop()`
    // (the producer hasn't yet noticed its join request); turning that race
    // into an assertion would make orderly teardown brittle. The producer's
    // `Envelope` is destroyed in place, returning its allocation through the
    // allocator just like a normal drain.
    void post(Envelope&& env) {
        if (stop_requested_.load(std::memory_order_acquire)) {
            return;  // dropped — see comment above
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Re-check under the lock: stop() takes the same mutex before
            // setting the flag, so this guarantees no envelope enters the
            // queue after stop()'s critical section completes.
            if (stop_requested_.load(std::memory_order_relaxed)) {
                return;
            }
            queue_.push_back(std::move(env));
        }
        cv_.notify_one();
    }

    // Test/composition accessor: get a module by type.
    template <typename M>
    M& get() {
        CORTEXFLOW_ASSERT(modules_.has_value(),
            "Runtime::get<>() called before start() or after shutdown()");
        return std::get<M>(*modules_);
    }

    // Cache accessor. Valid only between start() and shutdown().
    cache_type& cache() {
        CORTEXFLOW_ASSERT(cache_.has_value(),
            "Runtime::cache() called before start() or after shutdown()");
        return *cache_;
    }

    // Clock accessor. Valid for the lifetime of the Runtime — the clock is
    // bound at construction, not at start(), so subsystems wired in
    // constructor bodies can already read it.
    Clock& clock() noexcept { return *clock_; }

    // Timer service accessor. Valid for the lifetime of the Runtime; the
    // service installs its advance handler on the clock at Runtime
    // construction so `ManualClock::advance` fires due timers from any
    // point. Arming before `start()` is allowed but firing requires the
    // post sink, which is bound during `start()`.
    TimerService& timers() noexcept { return timers_; }

    // Test introspection: current queue depth.
    std::size_t queue_size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    // Minimum cadence at which `run()` wakes when no timer is armed. Keeps
    // the loop responsive to `stop()` and to timers armed from inside a
    // handler whose CV-wakeup did not race with this iteration's wait. Set
    // to 1 ms per issue 20 — sub-millisecond cadence is a separate concern
    // (CPU usage, kernel scheduler granularity) and explicitly out of
    // scope. Not configurable via composition by design; a future host
    // build that wants a different value can change this constant locally.
    static constexpr std::chrono::milliseconds kMinTickInterval{1};

    static SteadyClock& default_steady_clock() {
        static SteadyClock clk;
        return clk;
    }

    // Compute how long `run()`'s `cv_.wait_until` should block before the
    // next `fire_due` pass. Capped at `kMinTickInterval` so that:
    //  - foreign-thread `arm()` calls (which don't notify the CV) are
    //    observed within a tick;
    //  - timers armed under `ManualClock` (whose `now()` only advances on
    //    explicit `advance(...)`) don't trap the loop in a wall-clock sleep
    //    that ignores the test driver;
    //  - `stop()` from a thread whose notify raced the wait still gets
    //    observed within a tick (AC5).
    // Below the cap we use the earliest live timer's remaining time when
    // that is sooner — a timer with 0.3 ms to go shouldn't wait the full
    // tick — and clamp to zero for already-due timers so they fire on the
    // immediate next iteration.
    std::chrono::steady_clock::duration compute_wait_budget() const {
        const auto cap =
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                kMinTickInterval);
        const auto next = timers_.next_due_at();
        if (!next) {
            return cap;
        }
        const auto remaining = *next - clock_->now();
        if (remaining <= Clock::duration::zero()) {
            return std::chrono::steady_clock::duration::zero();
        }
        const auto remaining_steady =
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                remaining);
        return remaining_steady < cap ? remaining_steady : cap;
    }

    static void post_trampoline(void* ctx, Envelope&& env) {
        static_cast<Runtime*>(ctx)->post(std::move(env));
    }

    template <typename Tuple, std::size_t... Is>
    void bind_each(Tuple& t, std::index_sequence<Is...>) {
        (std::get<Is>(t).bind_post(&post_trampoline, this), ...);
    }

    template <typename Tuple, std::size_t... Is>
    void on_start_each(Tuple& t, std::index_sequence<Is...>) {
        (std::get<Is>(t).on_start(), ...);
    }

    template <typename Tuple, std::size_t... Is>
    void on_stop_reverse(Tuple& t, std::index_sequence<Is...>) {
        constexpr std::size_t N = sizeof...(Is);
        // Fold-expression evaluates left-to-right; emit calls in reverse.
        (std::get<N - 1 - Is>(t).on_stop(), ...);
    }

    void dispatch(Envelope& env) {
        type_id_t to = env.to();
        // Resolve `from` and `to` to printable names once per envelope.
        // `kNoSender` (foreign-thread / boundary post) renders as `-`; an
        // id present in the `ModuleList` renders as that module's
        // `kName`; anything else (including the `kSystemSender`
        // flow-init sentinel) renders as `unknown:<hex>` so the trace
        // log is still debuggable. Module names live in static
        // `type_name_cstr<>()` storage; the hex fallback uses a stack
        // buffer that survives the trace_emit call below.
        char from_buf[40];
        char to_buf[40];
        char type_buf[48];
        const char* from_name =
            resolve_sender_name(env.from(), from_buf, sizeof(from_buf));
        const char* to_name = nullptr;
        const char* type_label = nullptr;
        resolve_target(*modules_,
                       std::make_index_sequence<kNumModules>{},
                       to, env.payload_type_id(), to_name, type_label);
        if (to_name == nullptr) {
            std::snprintf(to_buf, sizeof(to_buf), "unknown:%llx",
                          static_cast<unsigned long long>(to));
            to_name = to_buf;
        }
        if (type_label == nullptr) {
            std::snprintf(type_buf, sizeof(type_buf), "type:%llx",
                          static_cast<unsigned long long>(env.payload_type_id()));
            type_label = type_buf;
        }
        CORTEXFLOW_TRACE_DISPATCH(
            "envelope", from_name, to_name, type_label, "");
        bool dispatched = false;
        dispatch_one(*modules_,
                     std::make_index_sequence<kNumModules>{},
                     env, to, dispatched);
        CORTEXFLOW_ASSERT(dispatched,
            "envelope addressed to module not in ModuleList");
    }

    template <typename Tuple, std::size_t... Is>
    void dispatch_one(Tuple& t, std::index_sequence<Is...>,
                      Envelope& env, type_id_t to, bool& dispatched) {
        auto try_one = [&](auto& mod) {
            if (!dispatched && mod.module_type_id() == to) {
                mod.handle(env);
                dispatched = true;
            }
        };
        (try_one(std::get<Is>(t)), ...);
    }

    const char* resolve_sender_name(type_id_t id,
                                    char* buf, std::size_t buf_size) const {
        if (id == kNoSender) {
            return "-";
        }
        const char* resolved = nullptr;
        resolve_sender_name_one(*modules_,
                                std::make_index_sequence<kNumModules>{},
                                id, resolved);
        if (resolved != nullptr) {
            return resolved;
        }
        std::snprintf(buf, buf_size, "unknown:%llx",
                      static_cast<unsigned long long>(id));
        return buf;
    }

    template <typename Tuple, std::size_t... Is>
    void resolve_sender_name_one(const Tuple& t, std::index_sequence<Is...>,
                                 type_id_t id,
                                 const char*& out) const {
        auto try_one = [&](const auto& mod) {
            using ModT = std::decay_t<decltype(mod)>;
            if (out == nullptr && mod.module_type_id() == id) {
                out = type_name_cstr<ModT>();
            }
        };
        (try_one(std::get<Is>(t)), ...);
    }

    // Resolve both `to` (module-name) and the payload type-name in one
    // walk of the ModuleList: the matching module is the only one that
    // can answer the payload-name lookup, so colocating the search keeps
    // the per-envelope cost a single pass over the (always small) tuple.
    template <typename Tuple, std::size_t... Is>
    void resolve_target(const Tuple& t, std::index_sequence<Is...>,
                        type_id_t to_id, type_id_t payload_id,
                        const char*& to_name,
                        const char*& payload_name) const {
        auto try_one = [&](const auto& mod) {
            using ModT = std::decay_t<decltype(mod)>;
            if (to_name == nullptr && mod.module_type_id() == to_id) {
                to_name = type_name_cstr<ModT>();
                payload_name = mod.trace_payload_name(payload_id);
            }
        };
        (try_one(std::get<Is>(t)), ...);
    }

    std::optional<modules_tuple> modules_;
    std::optional<cache_type> cache_;
    Clock* clock_;
    TimerService timers_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Envelope> queue_;
    std::atomic<bool> stop_requested_{false};
    bool started_ = false;
};

} // namespace cortexflow
