#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include <cortexflow/assert.hpp>
#include <cortexflow/messaging.hpp>
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
