#pragma once

#include <chrono>
#include <cstddef>

#include <cortexflow/cache.hpp>
#include <cortexflow/clock.hpp>
#include <cortexflow/runtime.hpp>
#include <cortexflow/timer.hpp>

#include "keys.hpp"
#include "modules/consumer.hpp"
#include "modules/producer.hpp"

namespace minimal_app {

// ---------------------------------------------------------------------------
// Composition — the single type-level declaration that names every module,
// every cache key (with its owner), and the runtime knobs. Mis-wirings here
// (duplicate module, send-to-unknown-target, etc.) are compile-time errors
// at the Runtime's static_asserts (PRD US 2, 8).
// ---------------------------------------------------------------------------

using Modules = cortexflow::ModuleList<Producer, Consumer>;

using Keys = cortexflow::CacheKeyList<
    cortexflow::Owned<Counter, Producer>>;

using AppConfig = cortexflow::Config<cortexflow::MaxSubscriptions<4>>;

using Runtime = cortexflow::Runtime<Modules, Keys, AppConfig>;

using AppCache = cortexflow::Cache<Keys, AppConfig::kMaxSubscriptions>;

// Delay used by the Consumer's Processing state when arming its timer. Kept
// here so the integration tests can advance ManualClock by exactly this
// duration and observe deterministic firing.
inline constexpr std::chrono::milliseconds kProcessingDelay{10};

// ---------------------------------------------------------------------------
// Cross-module access to the running runtime.
//
// State-locals (Idle's Subscription, Processing's Timer) and Producer's
// `on(Producer::Bump&)` need to reach the cache / timer service. There is no
// per-module back-ref in cortexflow v1, so the App's constructor installs a
// static pointer to itself; the helpers below resolve through it.
//
// Lifetime: the pointer is non-null between App construction and destruction
// — covering the entire `start → run/run_one → shutdown` window. Modules
// should only call these from handlers (or `on_start`), all of which run
// inside that window.
// ---------------------------------------------------------------------------

namespace detail {
extern Runtime* g_runtime;
}

inline AppCache& cache() { return detail::g_runtime->cache(); }
inline cortexflow::TimerService& timers() { return detail::g_runtime->timers(); }

// ---------------------------------------------------------------------------
// App — thin RAII wrapper around Runtime that publishes itself to the
// helpers above. Tests and main() see only the lifecycle surface; the
// underlying Runtime is reachable via `runtime()` for advanced cases (e.g.
// posting envelopes from integration tests).
// ---------------------------------------------------------------------------

class App {
public:
    App();
    explicit App(cortexflow::Clock& clock);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    void start()    { rt_.start(); }
    void run()      { rt_.run(); }
    void run_one()  { rt_.run_one(); }
    void stop()     { rt_.stop(); }
    void shutdown() { rt_.shutdown(); }

    void post(cortexflow::Envelope&& env) { rt_.post(std::move(env)); }
    std::size_t queue_size() const { return rt_.queue_size(); }

    template <typename M> M& get() { return rt_.template get<M>(); }
    AppCache& cache_ref() { return rt_.cache(); }
    cortexflow::TimerService& timers_ref() { return rt_.timers(); }

    Runtime& runtime() noexcept { return rt_; }

private:
    Runtime rt_;
};

}  // namespace minimal_app
