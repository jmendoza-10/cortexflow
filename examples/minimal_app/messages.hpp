#pragma once

namespace minimal_app {

// Bump — addressed to Producer; tells it to advance Counter by one.
// Producer self-posts a Bump in `on_start` to seed the system and re-posts
// after every Done ack so the loop keeps running under `app.run()`.
struct Bump {};

// Done — addressed to Producer by Consumer once a Counter change has been
// observed and processed. Demonstrates module-to-module send via the
// type-derived identity machinery (PRD US 7).
struct Done {};

// ProcessingTick — addressed to Consumer by its own Processing-state timer.
// The arrival of this message is what drives Consumer back out of Processing.
struct ProcessingTick {};

}  // namespace minimal_app
