#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <tuple>
#include <type_traits>
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

namespace detail {
struct StateInfo;
} // namespace detail

// ---------------------------------------------------------------------------
// StateDirective + factories
// ---------------------------------------------------------------------------
//
// Four directives, matching the PRD §"State directive encoding":
//   - Stay         — keep the current state; locals untouched.
//   - Transition   — destruct current locals, construct next state's locals,
//                    then return to the loop (next state runs on the *next*
//                    dispatched envelope).
//   - TransitionNow— destruct current locals, construct next state's locals,
//                    and immediately re-enter the new state with the same
//                    envelope. No chain-depth limit is enforced.
//   - Done         — destruct current locals, mark the flow inactive, then
//                    synchronously call `on_flow_done()` on the owning module
//                    before any other dispatch resumes.
struct StateDirective {
    enum class Kind { Stay, Transition, TransitionNow, Done };

    Kind kind;
    const detail::StateInfo* next;  // valid for Transition / TransitionNow
};

namespace detail {

// Default state-locals type for state tags that do not declare one. Sized to
// the minimum representable empty struct (1 byte) so it contributes nothing
// meaningful to the per-flow buffer's max-size computation.
struct EmptyLocals {};

template <typename T, typename = void>
struct locals_of { using type = EmptyLocals; };

template <typename T>
struct locals_of<T, std::void_t<typename T::Locals>> {
    using type = typename T::Locals;
};

template <typename T>
using locals_of_t = typename locals_of<T>::type;

// Per-state descriptor. CortexFlow stores a `const StateInfo*` as the
// runtime identity of each state — both the directive's `next` pointer and
// the flow's `current_` slot. Two states have distinct StateInfo addresses
// (each `kStateInfo<Tag>` is its own variable template instantiation), so
// pointer identity is the state identity.
struct StateInfo {
    StateDirective (*handle)(FlowCtx&, Envelope&);
    void (*construct_locals)(void* buffer);
    void (*destruct_locals)(void* buffer);
};

template <typename StateTag>
inline StateDirective handle_trampoline(FlowCtx& ctx, Envelope& env) {
    return StateTag::handle(ctx, env);
}

template <typename StateTag>
inline void construct_locals_trampoline(void* buffer) {
    using L = locals_of_t<StateTag>;
    ::new (buffer) L();
}

template <typename StateTag>
inline void destruct_locals_trampoline(void* buffer) {
    using L = locals_of_t<StateTag>;
    static_cast<L*>(buffer)->~L();
}

template <typename StateTag>
inline constexpr StateInfo kStateInfo = {
    &handle_trampoline<StateTag>,
    &construct_locals_trampoline<StateTag>,
    &destruct_locals_trampoline<StateTag>,
};

} // namespace detail

// ---------------------------------------------------------------------------
// StateList<StateTags...>
// ---------------------------------------------------------------------------
//
// Compile-time list of state-tag types belonging to a flow. The list drives
// the per-flow state-locals buffer's size and alignment: the buffer is sized
// to fit the largest `StateTag::Locals` across the list, aligned to the
// strictest alignment any of them requires. There is no runtime allocation
// per transition.
//
// A state tag is a type with two members:
//   - `static StateDirective handle(FlowCtx&, Envelope&)` — the state's
//     behavior.
//   - `using Locals = …;` — the typed locals struct constructed in the
//     flow's buffer on entry to this state, destructed on transition out.
//     If a tag omits `Locals`, an empty struct stands in (no storage cost).
template <typename... StateTags>
struct StateList {
    static_assert(sizeof...(StateTags) > 0,
        "cortexflow::StateList must contain at least one state-tag type");

    using head = std::tuple_element_t<0, std::tuple<StateTags...>>;

    // `std::max(initializer_list)` is constexpr since C++14.
    static constexpr std::size_t kMaxLocalsSize =
        std::max({sizeof(detail::locals_of_t<StateTags>)...});
    static constexpr std::size_t kMaxLocalsAlign =
        std::max({alignof(detail::locals_of_t<StateTags>)...});
};

[[nodiscard]] inline StateDirective stay() noexcept {
    return StateDirective{StateDirective::Kind::Stay, nullptr};
}

template <typename NextStateTag>
[[nodiscard]] inline StateDirective transition_to() noexcept {
    return StateDirective{
        StateDirective::Kind::Transition,
        &detail::kStateInfo<NextStateTag>,
    };
}

// Re-enter `NextStateTag` immediately with the *same* envelope. The flow
// loop destructs the outgoing locals and constructs the incoming locals,
// then invokes the new state's `handle` without returning from `step()`.
// The next state may use or ignore the envelope. Chain-depth is the flow
// designer's responsibility — CortexFlow does not enforce a limit.
template <typename NextStateTag>
[[nodiscard]] inline StateDirective transition_to_now() noexcept {
    return StateDirective{
        StateDirective::Kind::TransitionNow,
        &detail::kStateInfo<NextStateTag>,
    };
}

// End the flow: destruct current locals, mark inactive, and synchronously
// call `on_flow_done()` on the owning module before any other dispatch
// resumes. After `done`, the flow is dormant until `restart()`.
[[nodiscard]] inline StateDirective done() noexcept {
    return StateDirective{StateDirective::Kind::Done, nullptr};
}

// ---------------------------------------------------------------------------
// FlowCtx
// ---------------------------------------------------------------------------
//
// Per-step context handed to state functions. The only member of interest in
// this slice is `locals<L>()` — a typed view into the flow's state-locals
// buffer. The type `L` must match the locals type of the state that is
// currently constructed in the buffer; CortexFlow guarantees that by
// constructing exactly the entering state's locals on transition (slice 14)
// so the type at the buffer never disagrees with the active state.
class FlowCtx {
public:
    template <typename L>
    L& locals() noexcept {
        CORTEXFLOW_ASSERT(locals_ptr_ != nullptr,
            "cortexflow::FlowCtx::locals: no state-locals buffer bound "
            "(flow not started, or accessed outside a step)");
        return *static_cast<L*>(locals_ptr_);
    }

private:
    void* locals_ptr_ = nullptr;

    template <typename, typename, typename> friend class Flow;
};

// ---------------------------------------------------------------------------
// Flow<Owner, StateListT>
// ---------------------------------------------------------------------------
//
// A re-entrant state machine owned by exactly one module (architecture §8.1;
// PRD user stories 29, 30, 32, 33, 34).
//
// `Owner` is the owning module type — used as the destination type-id on the
// synthetic init envelope and reserved as the hook point for slice 15's
// `on_flow_done` callback.
//
// `StateListT` is `StateList<StateTags...>` — the compile-time list of every
// state-tag the flow may enter. The state-locals buffer is sized and aligned
// from this list; every state passed as a transition target must belong to
// it (otherwise the buffer is not guaranteed wide enough). `InitialStateTag`
// defaults to the first tag in the list and is overridable for tests or
// asymmetric flows.
template <typename Owner, typename StateListT,
          typename InitialStateTag = typename StateListT::head>
class Flow {
public:
    constexpr Flow() noexcept = default;

    ~Flow() {
        // Destruct the live state-locals if the flow is currently active.
        // `done` and `terminate` both flip `active_` false and destruct
        // eagerly; this teardown only fires for the live-on-shutdown case,
        // which is what releases RAII state-locals (Subscription, Timer)
        // when a module is destroyed mid-flow.
        if (active_) {
            current_->destruct_locals(locals_buffer_);
        }
    }

    Flow(const Flow&) = delete;
    Flow& operator=(const Flow&) = delete;
    Flow(Flow&&) = delete;
    Flow& operator=(Flow&&) = delete;

    // Dispatch the synthetic init envelope into the initial state. Called
    // once per flow lifetime, typically from the owning module's `on_start`.
    // The owner reference is captured so `done` can route `on_flow_done`
    // back to the owning module and `restart` can re-dispatch the init
    // envelope without the caller re-supplying it. The envelope is invoked
    // synchronously — by the time `start()` returns, the initial state has
    // already had its chance to register subscriptions and arm timers from
    // its locals' constructors.
    void start(Owner& owner) {
        CORTEXFLOW_ASSERT(!active_,
            "cortexflow::Flow::start: flow already started");
        owner_ = &owner;
        enter_initial_();
    }

    // Invoke the current state function with `env` and apply the returned
    // directive. The owning module's `handle(Envelope&)` delegates here.
    //
    // The body is a loop because `transition_to_now` re-enters the new
    // state with the same envelope synchronously; `Stay`, `Transition`,
    // and `Done` all exit the loop. `Done` finishes the loop, releases
    // `in_step_`, then synchronously dispatches `on_flow_done` to the
    // owner — so a handler that calls `flow.restart()` from within
    // `on_flow_done` does not trip the "inside step" assert.
    void step(Envelope& env) {
        CORTEXFLOW_ASSERT(active_,
            "cortexflow::Flow::step: called before start()");
        CORTEXFLOW_ASSERT(!in_step_,
            "cortexflow::Flow::step: reentrant invocation");
        in_step_ = true;

        bool done_pending = false;
        while (true) {
            StateDirective dir = current_->handle(ctx_, env);
            if (dir.kind == StateDirective::Kind::Stay) {
                break;
            }
            if (dir.kind == StateDirective::Kind::Transition ||
                dir.kind == StateDirective::Kind::TransitionNow) {
                CORTEXFLOW_ASSERT(dir.next != nullptr,
                    "cortexflow::Flow: transition directive carries null next");
                current_->destruct_locals(locals_buffer_);
                current_ = dir.next;
                current_->construct_locals(locals_buffer_);
                ctx_.locals_ptr_ = locals_buffer_;
                if (dir.kind == StateDirective::Kind::Transition) {
                    break;
                }
                continue;  // TransitionNow: re-invoke with the same envelope
            }
            // Kind::Done — destruct, mark inactive, defer the owner callback
            // until after the in_step_ flag is cleared (so on_flow_done is
            // free to call restart() / terminate() without asserting).
            current_->destruct_locals(locals_buffer_);
            ctx_.locals_ptr_ = nullptr;
            active_ = false;
            done_pending = true;
            break;
        }

        in_step_ = false;

        if (done_pending) {
            CORTEXFLOW_ASSERT(owner_ != nullptr,
                "cortexflow::Flow::done: owner reference not bound "
                "(start() must be called before a state can return done())");
            owner_->on_flow_done();
        }
    }

    // Force-end the flow from *outside* a `step` call (e.g., from a module's
    // `handle` for a non-flow message, or from `on_flow_done`). Inside a
    // step, a state must use the `done()` directive instead.
    //
    // No-op if the flow is not currently active (already done, terminated,
    // or never started). This makes terminate idempotent and safe to call
    // unconditionally from module teardown paths.
    void terminate() {
        CORTEXFLOW_ASSERT(!in_step_,
            "cortexflow::Flow::terminate: cannot be called from inside a "
            "step (return the done() directive instead)");
        if (active_) {
            current_->destruct_locals(locals_buffer_);
            ctx_.locals_ptr_ = nullptr;
            active_ = false;
        }
    }

    // Begin a fresh run from the initial state with newly constructed
    // locals. Re-dispatches the synthetic init envelope, just like
    // `start`. Requires that `start` has already been called once (so
    // the owner reference is bound).
    //
    // Like `terminate`, `restart` is illegal from inside a step — a state
    // wanting to re-enter the initial state should return
    // `transition_to<InitialState>()` or `done()` followed by a restart
    // from `on_flow_done`.
    void restart() {
        CORTEXFLOW_ASSERT(!in_step_,
            "cortexflow::Flow::restart: cannot be called from inside a step");
        CORTEXFLOW_ASSERT(owner_ != nullptr,
            "cortexflow::Flow::restart: owner reference not bound "
            "(call start() at least once before restart())");
        if (active_) {
            current_->destruct_locals(locals_buffer_);
            ctx_.locals_ptr_ = nullptr;
            active_ = false;
        }
        enter_initial_();
    }

    // Test introspection: which state will run on the next `step`.
    const detail::StateInfo* current() const noexcept { return current_; }
    bool started() const noexcept { return active_; }

    static constexpr std::size_t kLocalsBufferSize =
        StateListT::kMaxLocalsSize;
    static constexpr std::size_t kLocalsBufferAlign =
        StateListT::kMaxLocalsAlign;

private:
    // Shared entry path for `start` and `restart`: reset to the initial
    // state, construct its locals, and dispatch the synthetic init envelope
    // synchronously so the initial state can set up subscriptions / timers
    // before any external send.
    void enter_initial_() {
        current_ = initial_;
        active_ = true;
        current_->construct_locals(locals_buffer_);
        ctx_.locals_ptr_ = locals_buffer_;
        auto ptr = make_message<FlowInit>();
        Envelope env(kSystemSender, type_id<Owner>(), std::move(ptr));
        step(env);
    }

    alignas(kLocalsBufferAlign) std::byte locals_buffer_[kLocalsBufferSize]{};

    const detail::StateInfo* initial_ = &detail::kStateInfo<InitialStateTag>;
    const detail::StateInfo* current_ = initial_;
    Owner* owner_ = nullptr;
    FlowCtx ctx_{};
    bool active_ = false;
    bool in_step_ = false;
};

} // namespace cortexflow
