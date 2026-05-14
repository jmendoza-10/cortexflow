#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <cortexflow/assert.hpp>
#include <cortexflow/messaging.hpp>
#include <cortexflow/module.hpp>
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

template <typename Key, typename Owner>
struct Owned {
    using key_type = Key;
    using owner_type = Owner;
};

template <typename... Keys>
struct CacheKeyList {
    static constexpr std::size_t size = sizeof...(Keys);
};

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
    static constexpr std::size_t kNumModules =
        std::tuple_size_v<modules_tuple>;
    static constexpr std::size_t kDrainBudget = ConfigT::kDrainBudget;

    Runtime() = default;
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
    void start() {
        CORTEXFLOW_ASSERT(!started_, "Runtime::start called twice");
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
    void run() {
        CORTEXFLOW_ASSERT(started_, "Runtime::run before start()");
        while (true) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [&] {
                    return !queue_.empty() || stop_requested_.load();
                });
                if (queue_.empty() && stop_requested_.load()) {
                    return;
                }
            }
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

    // Test introspection: current queue depth.
    std::size_t queue_size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
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

    std::optional<modules_tuple> modules_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Envelope> queue_;
    std::atomic<bool> stop_requested_{false};
    bool started_ = false;
};

} // namespace cortexflow
