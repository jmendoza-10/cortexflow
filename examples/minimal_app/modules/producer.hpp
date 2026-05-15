#pragma once

#include <tuple>

#include <cortexflow/module.hpp>

#include "../messages.hpp"

namespace minimal_app {

// Producer — the writer of the Counter cache key.
//
// Inbox:
//   - Bump: increment the internal counter and publish the new value to the
//     cache. Triggered first by Producer's own `on_start` (seeding the system)
//     and afterwards by every Done ack from Consumer.
//   - Done: ack from Consumer that the previous Counter change was processed.
//     Producer counts acks and re-posts a Bump to keep the loop running so
//     `app.run()` has work to do under the demo's continuous-loop pattern.
//
// The module deliberately holds no pointer to the cache; it reaches it via
// the app-wide `minimal_app::cache()` helper (see app.hpp), which resolves to
// the currently-running Runtime's cache. That helper is set up by the App
// wrapper's constructor before `start()`, so it is valid for the entire
// lifetime of any module instance.
class Producer : public cortexflow::Module<Producer> {
public:
    using Inbox = std::tuple<Bump, Done>;

    void on_start() override;
    void on(Bump&);
    void on(Done&);

    int counter() const noexcept { return counter_; }
    int acks() const noexcept { return acks_; }

private:
    int counter_ = 0;
    int acks_ = 0;
};

}  // namespace minimal_app
