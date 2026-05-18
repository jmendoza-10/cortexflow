# Architecture Decision Records

Each ADR captures one significant architectural decision: the context, the choice, the consequences, the alternatives considered, and the rejection rationale. They are short — one page or so — and written while the reasoning is still fresh.

## Convention

- File name: `NNNN-short-title.md` (zero-padded sequential ID, kebab-case title).
- Status field at the top: `Proposed`, `Accepted`, `Superseded by ADR-NNNN`, or `Deprecated`.
- One decision per ADR. If a later ADR overturns an earlier one, the earlier one stays in place with status `Superseded by ADR-NNNN`.

## Template

```markdown
# ADR-NNNN: <title>

**Status:** Accepted
**Date:** YYYY-MM-DD

## Context

What problem are we solving? What constraints apply?

## Decision

The choice, stated plainly.

## Consequences

What this enables. What it costs. What it forbids.

## Alternatives considered

For each:
- What it was.
- Why it was rejected.
```

## Written

| ID   | Decision                                                                                              |
|------|-------------------------------------------------------------------------------------------------------|
| 0020 | [Receiver-owned messages nested in the handling module](0020-receiver-owned-messages.md)              |
| 0021 | [Generated Flow diagrams and Module graphs from C++ source](0021-generated-diagrams-from-cpp-source.md) |
| 0022 | [PRIVATE compile flags for `-fno-rtti` and `-fno-exceptions`](0022-private-compile-flags-for-rtti-and-exceptions.md) |
| 0023 | [Release-packaging strategy for v0.1.0+](0023-release-packaging-strategy.md)                          |

## Planned initial ADRs

The architectural spine locked in the v1 design produced the decisions below. Slots **001–019** are reserved for these; each should become an ADR before implementation begins on the area it covers. ADRs unrelated to the v1 spine take the next available number after the highest written ADR (currently 0023).

| ID  | Decision                                                          |
|-----|-------------------------------------------------------------------|
| 001 | Single-threaded run-to-completion event loop                      |
| 002 | Single FIFO message queue, no priority lanes                      |
| 003 | One-instance-per-type module addressing                           |
| 004 | Envelope = `{ to, from, MessagePtr }`; dispatch at receiver       |
| 005 | Pluggable allocator with platform-default backends                |
| 006 | Type-derived identity via `type_name<T>()`                        |
| 007 | Smart data cache with typed keys and dynamic RAII subscriptions   |
| 008 | One Flow per module (v1)                                          |
| 009 | State functions return `StateDirective`; `transition_to_now`      |
| 010 | Per-state RAII state-locals with framework-managed lifetime       |
| 011 | Timer service as runtime-level facility                           |
| 012 | Clock injected at runtime construction                            |
| 013 | Two-phase lifecycle                                               |
| 014 | Type-level composition (`Runtime<ModuleList, CacheKeyList, ...>`) |
| 015 | Six-level trace hierarchy with `if constexpr` disable             |
| 016 | Single `CORTEXFLOW_ASSERT`; framework errors are unrecoverable    |
| 017 | Boundary modules by convention, no enforced base class            |
| 018 | One declared writer per cache key, convention not compile-time    |
| 019 | Condition-variable wake (no eventfd/epoll dependency)             |

Full design reference: [`../architecture.md`](../architecture.md).
