#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include <cortexflow/assert.hpp>
#include <cortexflow/messaging.hpp>
#include <cortexflow/type_name.hpp>

namespace cortexflow {

// ---------------------------------------------------------------------------
// Sentinel sender for system-injected envelopes (PRD user story 35).
// Distinct from `kNoSender` (= 0, used by platform-injected / fire-and-forget
// posts) so state functions can tell the synthetic init envelope apart from
// an envelope that simply has no declared sender.
inline constexpr type_id_t kSystemSender =
    ~static_cast<type_id_t>(0);

// Payload carried by the synthetic init envelope dispatched on
// `Flow::start`. The envelope has "no payload" semantically — initial states
// detect first entry through `env.from() == kSystemSender` rather than
// inspecting the payload type — but `Envelope` requires a typed payload, so
// a one-byte sentinel struct stands in.
struct FlowInit {};

class FlowCtx;
struct StateDirective;

// State functions are free functions with a uniform signature. Any state
// function can be passed as `next` to any other (PRD user story 39): flow
// shapes are not constrained to a static graph.
using StateFn = StateDirective (*)(FlowCtx&, Envelope&);

struct StateDirective {
    // `transition_to_now` and `done` arrive in slice 15; the encoding shown
    // in the PRD (§"State directive encoding") is built out incrementally.
    enum class Kind { Stay, Transition };

    Kind kind;
    StateFn next;
};

[[nodiscard]] inline StateDirective stay() noexcept {
    return StateDirective{StateDirective::Kind::Stay, nullptr};
}

[[nodiscard]] inline StateDirective transition_to(StateFn next) noexcept {
    CORTEXFLOW_ASSERT(next != nullptr,
        "cortexflow::transition_to: next state function is null");
    return StateDirective{StateDirective::Kind::Transition, next};
}

// Per-state context handed to state functions. Reserved for slice 14's
// state-locals accessor (`locals<L>()`); intentionally empty in this slice.
class FlowCtx {
};

// ---------------------------------------------------------------------------
// Flow<Owner>
// ---------------------------------------------------------------------------
//
// A re-entrant state machine owned by exactly one module (architecture §8.1;
// PRD user stories 29, 30). `Owner` is the owning module type — used to
// address the synthetic init envelope to the owner and reserved as the hook
// point for slice 15's `on_flow_done` callback.
//
// State-locals storage is not part of this slice (see slice 14). The fixed
// placeholder buffer below sits where the compile-time-sized aligned storage
// will go; nothing constructs into it yet.
template <typename Owner>
class Flow {
public:
    explicit Flow(StateFn initial) noexcept
        : initial_(initial), current_(initial) {
        CORTEXFLOW_ASSERT(initial != nullptr,
            "cortexflow::Flow: initial state function is null");
    }

    Flow(const Flow&) = delete;
    Flow& operator=(const Flow&) = delete;
    Flow(Flow&&) = delete;
    Flow& operator=(Flow&&) = delete;

    // Dispatch the synthetic init envelope into the initial state. Called
    // once per flow lifetime, typically from the owning module's `on_start`.
    // The envelope is invoked synchronously — by the time `start()` returns
    // the initial state has already had its chance to register subscriptions
    // and arm timers.
    void start() {
        CORTEXFLOW_ASSERT(!started_,
            "cortexflow::Flow::start: flow already started");
        started_ = true;
        auto ptr = make_message<FlowInit>();
        Envelope env(kSystemSender, type_id<Owner>(), std::move(ptr));
        step(env);
    }

    // Invoke the current state function with `env` and apply the returned
    // directive. The owning module's `handle(Envelope&)` delegates here.
    void step(Envelope& env) {
        CORTEXFLOW_ASSERT(started_,
            "cortexflow::Flow::step: called before start()");
        StateDirective dir = current_(ctx_, env);
        switch (dir.kind) {
            case StateDirective::Kind::Stay:
                break;
            case StateDirective::Kind::Transition:
                CORTEXFLOW_ASSERT(dir.next != nullptr,
                    "cortexflow::Flow: transition directive carries null next");
                current_ = dir.next;
                break;
        }
    }

    // Test introspection: which state function will run on the next `step`.
    StateFn current() const noexcept { return current_; }
    bool started() const noexcept { return started_; }

private:
    // Placeholder state-locals buffer. Slice 14 sizes this at compile time
    // from the declared state list and constructs/destructs locals on
    // transition; in this slice it is reserved but untouched.
    static constexpr std::size_t kPlaceholderLocalsSize = 64;
    [[maybe_unused]] alignas(alignof(std::max_align_t))
        std::byte locals_buffer_[kPlaceholderLocalsSize]{};

    StateFn initial_;
    StateFn current_;
    FlowCtx ctx_{};
    bool started_ = false;
};

} // namespace cortexflow
