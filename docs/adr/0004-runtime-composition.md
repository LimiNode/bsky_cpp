# ADR-0004: Runtime composition and dependency ownership

- **Status:** Accepted direction for Stage 2; implementation deferred to M1c.
- **Date:** 2026-10-31
- **Depends on:** ADR-0002, ADR-0003, and [`DEPENDENCY_REUSE_AUDIT.md`](../research/DEPENDENCY_REUSE_AUDIT.md)

## Context

Stage 2 audited exact upstream snapshots of `event-hub-cpp`, `kurlyk`, and
`time-shield-cpp`. The foundation/value layer must remain dependency-independent,
but a production asynchronous Client needs one coherent scheduler, event
orchestration policy, and operation state machine. The accepted direction is to
use `event-hub-cpp` as the required runtime infrastructure for that production
Client, while keeping network, persistence, and application-level streams behind
replaceable or optional components.

This document is architectural policy, not implementation evidence. No runtime
target, dependency, or M1c executor exists in this repository yet.

## Decision

### Dependency boundary and future CMake topology

The future package topology is intentionally explicit:

| Component | Target | Dependency and role | Installation |
| --- | --- | --- | --- |
| Foundation | `bsky_cpp::bsky_cpp` | Current value/protocol/transport-neutral primitives | Default component; no optional dependency |
| Production runtime | `bsky_cpp::runtime` | Requires `event-hub-cpp::event-hub-cpp` and Threads; owns scheduling/orchestration composition | Opt-in component; installed config calls `find_dependency(event-hub-cpp CONFIG)` |
| Client + native transport adapter | `bsky_cpp::kurlyk_adapter` | Requires runtime and the pinned kurlyk package; forwards native completion without exposing kurlyk DTOs | Opt-in adapter component; exported transitive dependencies are explicit |
| Streams | future `bsky_cpp::streams` | Optional HTTP/WebSocket source and bsky replay/backpressure contract | Opt-in; not required by one-shot Client operations |
| Persistence | future `bsky_cpp::persistence` | Optional MDBX-backed storage | Opt-in |

The foundation target continues to configure, build, install, and test without
initialized submodules. The runtime target is the one place where
`event-hub-cpp` becomes mandatory for the production async Client. Application
event subscriptions are not the same as runtime infrastructure and remain
optional at the public API boundary.

The implementation PR must prefer installed package targets over nested
`add_subdirectory()` calls. If source composition is unavoidable, it must add
each pinned dependency once, avoid duplicate physical Time Shield copies, set
Time Shield NTP explicitly `OFF`, and isolate upstream cache variables in a
documented preset/toolchain. Installed package configs must use
`find_dependency()` for every transitive target they expose. The audited
event-hub CMake currently uses `add_subdirectory()` for Time Shield and `FORCE`
for several cache options, so this is a required integration constraint rather
than an implementation detail.

### Managed and external execution

The production runtime supports two modes over the same event-hub primitives:

- **Managed:** the runtime owns an event-hub `ModuleHub` lifetime lease and
  starts/stops it according to an explicit policy. Upstream modules own their
  task managers and private workers. This mode is not considered conforming for
  timed awaiters until the upstream deadline correction described below lands.
- **External:** the caller owns the loop. The runtime installs a non-owning
  notifier, calls `process_once()`, and explicitly aggregates module task,
  EventBus awaiter, and transport deadlines. It must not create a parallel
  TaskManager, EventBus, or client run loop.

The pinned `ModuleHub::next_deadline()` does not aggregate
`EventBus::next_awaiter_deadline()`. `ModuleHub::run_loop()` can therefore wait
forever when the only pending work is an awaiter timeout. The current public API
does not expose a configurable managed polling cap. Consequently:

- external mode must aggregate the bus awaiter deadline itself;
- managed mode must require an upstream event-hub correction before promising
  awaiter timeout delivery, shutdown wake-up, or timeout-driven lifetime
  cleanup; and
- this ADR does not claim that `ModuleHub::start()` or `run()` supports a local
  polling-cap workaround.

### Ownership, cancellation, shutdown, and callbacks

The bsky operation state machine owns the first-winner terminal outcome. Each
accepted operation retains shared state and the runtime/executor lease through
terminal completion and callback quiescence. A borrowed external runtime is
accepted only with an explicit caller lifetime guarantee. Destroying `Client`
stops admission and initiates shutdown without invalidating operation state or
callback captures.

Transport cancellation is cooperative and may race with completion. Callback
registration closes independently: close-before-start suppresses a queued
callback, while an already-running callback may finish. Quiescence includes
queued registrations that are delivered or suppressed and every in-flight
callback. Shutdown reports `QuiescencePending` or `ShutdownTimedOut` when its
deadline expires.

The callback-first `OperationHandle` contract in ADR-0002 is unchanged. Any
change to callback-first delivery, handle ownership, or the mutually exclusive
future adapter requires a new ADR.

`kurlyk::NetworkWorker` is reused for native async HTTP. A second network worker
or per-request waiter thread is prohibited. Because `kurlyk::HttpClient`
destruction blocks while group cancellation callbacks are delivered, the
runtime must retain a process-level network-worker lease and must never destroy
the last client from a kurlyk worker callback. Closing one bsky Client must not
stop infrastructure used by another Client.

### Transport completion, retries, and rate limits

The future kurlyk adapter forwards native completion directly into operation
state; it never calls `future.get()` from an event loop or callback. Admission
failure is synchronous and produces no callback. Post-admission executor or
transport rejection is one terminal typed error. Kurlyk executes only retries
authorized by bsky's operation/idempotency policy. Unsafe side effects disable
automatic retry unless protocol-validated idempotency is available, and an
uncertain side effect may still produce `OutcomeUnknown`.

Kurlyk owns wire retries, rate-limit handles, sequential tokens, and partition
state. bsky owns XRPC mapping, retry safety, and the meaning of terminal
outcomes; it must not build a second token bucket.

### Clock and test policy

The pinned event-hub TaskManager and EventAwaiter call
`std::chrono::steady_clock::now()` directly and expose no clock-provider API.
M1c therefore separates:

1. deterministic operation-state tests with a fake executor and controlled
   completion/cancellation events;
2. scheduler integration tests using real monotonic deadlines and bounded waits;
3. an optional upstream improvement for injectable clock/time providers if
   full virtual-time scheduler testing is required.

No conformance claim may depend on a nonexistent fake-clock hook, tight sleeps,
or unbounded polling.

## Upstream corrective proposals

### event-hub-cpp: awaiter deadline aggregation

At the pinned SHA, add `EventBus::next_awaiter_deadline()` to the aggregation
used by `ModuleHub::next_deadline()` and cover both `run()` and `start()` with a
deterministic regression test: register a timeout-only awaiter, enqueue no
events and no other deadlines, then assert the timeout fires and shutdown
wakes/joins within a bounded deadline. Add an external `process_once()` case to
confirm the same deadline is exposed. This proposal belongs in the upstream
repository; PR #9 does not modify it.

### time-shield-cpp: fractional-second contract

The parser documents 1–9 fractional digits but rejects the fourth digit. The
upstream correction must define whether values are retained at nanosecond
precision or deliberately truncated/rounded to milliseconds, then add positive
fixtures for 1/3/6/9 digits and a negative ten-digit fixture. Until released,
bsky preserves the original Lexicon datetime string and does not round-trip
through a lossy millisecond formatter.

## Milestone-scoped conformance gates

### M1c — Operation Runtime

Required: managed/external composition (with the managed awaiter limitation
resolved), bounded operation admission, synchronous pre-admission rejection,
exactly-one terminal outcome, cancellation/completion first-winner behavior,
callback closure/suppression, move-only callbacks, post-admission executor
rejection, shutdown/quiescence, lifetime leases, destruction safety, exception
containment, and deterministic fake execution/race tests.

M1c must not require kurlyk, XRPC, WebSocket, OAuth, persistence, or Time
Shield Lexicon integration. The foundation remains dependency-independent.

### M1d — Native Transport/XRPC Integration

Required when implemented: native kurlyk completion forwarding, HTTP/status/XRPC
mapping, retry/idempotency semantics, rate-limit partitions and token release,
response-header conversion, transport cancellation, XRPC encoding/decoding,
timestamp validation, Lexicon string preservation, installed consumer checks,
and dependency export checks.

### M4 — Advanced Infrastructure

Required when implemented: WebSocket reconnect, Jetstream replay/checkpoints/
gaps, persistent recovery, AT Protocol OAuth, streaming backpressure, and
network-specific reconciliation.

## Consequences

This composition reuses tested upstream scheduling and HTTP primitives without
making them mandatory for foundation consumers. It gives the production Client
one runtime owner, prevents duplicate workers/schedulers, and makes package
installation consequences explicit. It also means managed awaiter timeouts are
blocked on an upstream correction, while deterministic M1c operation semantics
can proceed with fake execution and bounded real-clock integration tests.

The existing M1b synchronous `Transport::send()` seam remains valid and is not
replaced by this ADR. It is a low-level fake-friendly seam, not the application
async implementation.

## Open questions

- Which event-hub upstream release will include the deadline aggregation fix?
- What queue bounds and fairness policy are appropriate for one-shot operations
  versus streams?
- Which ordered-header representation should kurlyk expose upstream, or should
  bsky explicitly weaken its wire-order contract?
- Which official AT Protocol OAuth profile and fixture revision will gate M4?
