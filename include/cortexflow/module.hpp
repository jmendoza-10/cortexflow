// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include <cortexflow/assert.hpp>
#include <cortexflow/messaging.hpp>
#include <cortexflow/trace.hpp>
#include <cortexflow/type_name.hpp>

namespace cortexflow {

class ModuleBase {
public:
    using PostFn = void(*)(void*, Envelope&&);

    virtual void handle(Envelope& env) = 0;
    virtual void on_start() {}
    virtual void on_stop() {}
    virtual void on_flow_done() {}

    virtual type_id_t module_type_id() const = 0;

    // Resolve the printable name of a payload type the module accepts. Used
    // by the DISPATCH-level trace at the runtime dispatch site. Returns
    // nullptr if the type is not in the module's `Inbox` or opt-in
    // `TraceTypes` declaration; the runtime then falls back to a hex
    // rendering of the type id. Modules can extend coverage by declaring
    // `using TraceTypes = std::tuple<...>` alongside `Inbox` — names
    // listed there are reported even when the module's own `handle`
    // dispatches manually (e.g. through a `Flow`).
    virtual const char* trace_payload_name(type_id_t) const noexcept {
        return nullptr;
    }

    void bind_post(PostFn fn, void* ctx) {
        post_fn_ = fn;
        post_ctx_ = ctx;
    }

protected:
    ~ModuleBase() = default;

    void post(Envelope&& env) {
        CORTEXFLOW_ASSERT(post_fn_ != nullptr,
            "Module::post called without bound post sink");
        post_fn_(post_ctx_, std::move(env));
    }

private:
    PostFn post_fn_ = nullptr;
    void* post_ctx_ = nullptr;
};

namespace detail {

// ----------------------------------------------------------------------------
// Composition-validator traits used by Module::send static_asserts. The
// traits also back the Runtime-level duplicate-module check (declared in
// runtime.hpp). Kept in module.hpp so that Module<Derived, ModuleListT> can
// reach them without including runtime.hpp.
// ----------------------------------------------------------------------------

template <typename Tuple, typename T>
struct tuple_contains : std::false_type {};

template <typename T, typename... Ts>
struct tuple_contains<std::tuple<Ts...>, T>
    : std::disjunction<std::is_same<T, Ts>...> {};

template <typename Tuple, typename T>
inline constexpr bool tuple_contains_v = tuple_contains<Tuple, T>::value;

template <typename, typename = void>
struct has_inbox : std::false_type {};

template <typename T>
struct has_inbox<T, std::void_t<typename T::Inbox>> : std::true_type {};

template <typename T>
inline constexpr bool has_inbox_v = has_inbox<T>::value;

// Pattern-match on any variadic template (e.g. ModuleList<Modules...>) so the
// trait does not need to include the runtime header.
template <typename List, typename T>
struct list_contains : std::false_type {};

template <template <typename...> class List, typename T, typename... Ts>
struct list_contains<List<Ts...>, T>
    : std::disjunction<std::is_same<T, Ts>...> {};

template <typename List, typename T>
inline constexpr bool list_contains_v = list_contains<List, T>::value;

struct DispatchEntry {
    type_id_t msg_type_id;
    void (*handler)(ModuleBase*, Envelope&);
};

template <typename Derived, typename Msg>
void dispatch_handler(ModuleBase* base, Envelope& env) {
    static_cast<Derived*>(base)->on(env.payload<Msg>());
}

template <typename Derived, typename Inbox>
struct DispatchTable;

template <typename Derived, typename... Msgs>
struct DispatchTable<Derived, std::tuple<Msgs...>> {
    static constexpr std::size_t kSize = sizeof...(Msgs);
    static constexpr DispatchEntry kEntries[sizeof...(Msgs)] = {
        { type_id<Msgs>(), &dispatch_handler<Derived, Msgs> }...
    };
};

template <typename Derived>
struct DispatchTable<Derived, std::tuple<>> {
    static constexpr std::size_t kSize = 0;
};

// Per-module name lookup table for the DISPATCH-level trace. Walked once
// per envelope; covers any type listed in `Inbox` (so Inbox-driven
// modules need no extra work) and any extra type listed in the optional
// `TraceTypes` typedef on the module (so Flow-driven modules can still
// surface human-readable names without going through the dispatch table).
template <typename T, typename = void>
struct trace_types_of {
    using type = std::tuple<>;
};

template <typename T>
struct trace_types_of<T, std::void_t<typename T::TraceTypes>> {
    using type = typename T::TraceTypes;
};

template <typename T>
using trace_types_of_t = typename trace_types_of<T>::type;

struct TraceNameEntry {
    type_id_t msg_type_id;
    const char* name;
};

template <typename...>
struct TraceNameTableFrom;

template <typename... Ts>
struct TraceNameTableFrom<std::tuple<Ts...>> {
    static constexpr std::size_t kSize = sizeof...(Ts);
    static constexpr TraceNameEntry kEntries[sizeof...(Ts) > 0
                                             ? sizeof...(Ts) : 1] = {
        { type_id<Ts>(), type_name_cstr<Ts>() }...
    };
};

// Specialisation for the empty case keeps the entries array at size 1 to
// remain a valid C++ aggregate (zero-length arrays are non-standard); the
// `kSize == 0` guard at the lookup site keeps the placeholder unused.
template <>
struct TraceNameTableFrom<std::tuple<>> {
    static constexpr std::size_t kSize = 0;
    static constexpr TraceNameEntry kEntries[1] = {{0, nullptr}};
};

template <typename... Tuples>
struct concat_tuples;

template <>
struct concat_tuples<> {
    using type = std::tuple<>;
};

template <typename... Ts>
struct concat_tuples<std::tuple<Ts...>> {
    using type = std::tuple<Ts...>;
};

template <typename... Ts1, typename... Ts2, typename... Rest>
struct concat_tuples<std::tuple<Ts1...>, std::tuple<Ts2...>, Rest...> {
    using type = typename concat_tuples<
        std::tuple<Ts1..., Ts2...>, Rest...>::type;
};

template <typename Derived>
struct TraceNameTable {
    using types = typename concat_tuples<
        typename Derived::Inbox, trace_types_of_t<Derived>>::type;
    using table = TraceNameTableFrom<types>;
};

} // namespace detail

// Module<Derived, ModuleListT>
//
// `ModuleListT` is optional. When supplied, `send<Target>(msg)` adds a
// compile-time check that `Target` is declared in the same `ModuleList`. The
// check is opt-in so existing modules using `Module<Derived>` are unaffected;
// modules that want call-site protection forward-declare their list and pass
// it as the second template argument.
template <typename Derived, typename ModuleListT = void>
class Module : public ModuleBase, public Identified<Derived> {
public:
    void handle(Envelope& env) override {
        envelope_ = &env;
        using Table = detail::DispatchTable<Derived, typename Derived::Inbox>;
        if constexpr (Table::kSize > 0) {
            for (std::size_t i = 0; i < Table::kSize; ++i) {
                if (Table::kEntries[i].msg_type_id == env.payload_type_id()) {
                    Table::kEntries[i].handler(this, env);
                    envelope_ = nullptr;
                    return;
                }
            }
        }
        envelope_ = nullptr;
        CORTEXFLOW_ASSERT(false, "message type not in module inbox");
    }

    type_id_t module_type_id() const override {
        return Identified<Derived>::kTypeId;
    }

    const char* trace_payload_name(type_id_t id) const noexcept override {
        using Table = typename detail::TraceNameTable<Derived>::table;
        if constexpr (Table::kSize > 0) {
            for (std::size_t i = 0; i < Table::kSize; ++i) {
                if (Table::kEntries[i].msg_type_id == id) {
                    return Table::kEntries[i].name;
                }
            }
        }
        return nullptr;
    }

protected:
    Envelope& envelope() { return *envelope_; }

    template <typename Msg>
    void reply_to(Envelope& env, Msg&& msg) {
        CORTEXFLOW_ASSERT(env.from() != kNoSender,
            "reply_to called on envelope with sentinel from");
        auto ptr = make_message<std::decay_t<Msg>>(std::forward<Msg>(msg));
        Envelope reply(Identified<Derived>::kTypeId, env.from(), std::move(ptr));
        this->post(std::move(reply));
    }

    template <typename Target, typename Msg>
    void send(Msg&& msg) {
        static_assert(detail::has_inbox_v<Target>,
            "cortexflow::Module::send: Target is not a cortexflow module "
            "(no Inbox typedef declared)");
        static_assert(
            detail::tuple_contains_v<typename Target::Inbox, std::decay_t<Msg>>,
            "cortexflow::Module::send: Target does not declare a handler "
            "for the message type (Msg is not listed in Target::Inbox)");
        if constexpr (!std::is_void_v<ModuleListT>) {
            static_assert(detail::list_contains_v<ModuleListT, Target>,
                "cortexflow::Module::send: Target is not declared in "
                "ModuleList (send target is not a registered module)");
        }
        auto ptr = make_message<std::decay_t<Msg>>(std::forward<Msg>(msg));
        Envelope env(Identified<Derived>::kTypeId, type_id<Target>(),
                     std::move(ptr));
        this->post(std::move(env));
    }

private:
    Envelope* envelope_ = nullptr;
};

} // namespace cortexflow
