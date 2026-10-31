# ADR-0004: Runtime composition and dependency ownership

- **Status:** Accepted direction for Stage 2; implementation deferred to M1c.
- **Date:** 2026-10-31
- **Depends on:** ADR-0002, ADR-0003, and [`DEPENDENCY_REUSE_AUDIT.md`](../research/DEPENDENCY_REUSE_AUDIT.md)

## Context

Stage 2 audited exact upstream snapshots of `event-hub-cpp`, `kurlyk`, and
`time-shield-cpp`. All three contain useful infrastructure, but importing their
threading and ownership assumptions directly into the public SDK would violate
the transport-neutral core and the callback/quiescence contract in ADR-0002.
The audit is source evidence only; this repository does not yet contain an
integration or an M1c executor implementation.

## Decision

### Execution modes

The future runtime adapter supports two modes:

- **Managed:** the adapter owns a lifetime lease for an `event-hub-cpp`
  `ModuleHub` and starts/stops it according to an explicit policy. Upstream
  modules own their task managers and private workers.
- **External:** the caller owns the event loop. The adapter installs a
  non-owning notifier, calls `process_once()`, and returns deadline hints for
  tasks, queued event awaiters, and transport completions. The adapter must not
  create a parallel task manager or client run loop.

External mode must aggregate `EventBus::next_awaiter_deadline()` explicitly,
because the audited `ModuleHub::next_deadline()` only checks module task
managers and module hints. A bounded idle cap is required until this upstream
behavior is clarified.

### Ownership and lifetime

The bsky client owns an executor/runtime lease. Each accepted operation retains
shared operation state and the executor lease through terminal completion and
callback quiescence. A borrowed external executor is accepted only with an
explicit caller lifetime guarantee. Destroying `Client` stops admission and
initiates shutdown; it does not invalidate operation state or callback captures.

`kurlyk::NetworkWorker` is reused for native async HTTP. A second network worker
or a per-request waiter thread is prohibited. `kurlyk::HttpClient` destruction
is blocking and must not occur on its worker thread or inside a callback.

### Cancellation, shutdown, and callback dispatch

The operation state machine remains the owner of the first-winner terminal
outcome. Transport cancellation is cooperative and may race with completion.
Callback registration is closed independently: close-before-start suppresses a
queued callback; an already-running callback may finish. Quiescence includes
queued registrations that are delivered or suppressed and every in-flight
callback. Shutdown reports `QuiescencePending` or `ShutdownTimedOut` when the
deadline expires; it must not silently claim success.

The callback-first `OperationHandle` contract in ADR-0002 is unchanged. Any
change to callback-first delivery, handle ownership, or the mutually exclusive
future adapter requires a new ADR.

### Transport completion, retries, and rate limits

The future kurlyk adapter forwards native completion directly into operation
state. It never calls `future.get()` from an event loop or callback. Admission
failure is synchronous and produces no callback; post-admission executor or
transport rejection is one terminal typed error. HTTP/XRPC status mapping,
retry safety, and `OutcomeUnknown` remain bsky policy. Kurlyk owns wire retries,
rate-limit handles, sequential tokens, and partition state; bsky must not build
a second token bucket or blindly retry side effects.

### Optional dependency installation

`bsky_cpp` core continues to configure, build, and test without any optional
dependency. A future integration may add explicitly pinned, flat submodules
under `external/`, with installation/export guarded by an opt-in CMake feature.
The public SDK must not leak kurlyk or event-hub types through high-level DTOs.
Source audits remain under `build/dependency-audit/` and are never installed.

### Upstream fixes and compatibility

Upstream fixes are proposed and tested in their owning repositories. In
particular, the audited time parser advertises 1–9 fractional digits but rejects
the fourth digit; bsky will not patch that header locally. Until a compatible
upstream release exists, the bsky adapter preserves the original Lexicon
datetime string and gates use of parsed precision. The event-hub deadline
omission is handled by explicit adapter aggregation and a regression test.

## Consequences

This composition reuses tested scheduling, HTTP, and time primitives without
making them mandatory for the core. It adds an adapter boundary and a conformance
matrix before M1c can be implemented. Applications must choose managed or
external execution and honor the documented lifetime lease. Queue bounds,
fairness, callback quiescence, protocol retry policy, and datetime round-trip
remain bsky-owned guarantees rather than accidental properties of upstream
containers.

## M1c conformance gate

M1c cannot be accepted until deterministic Debug/Release/sanitizer tests cover:

- managed/external loops, notifier wake-ups, and all deadline sources;
- admission, one terminal outcome, cancellation races, callback suppression,
  and executor shutdown;
- bounded queues and typed backpressure;
- client destruction and worker-thread blocking hazards;
- repeated response-header order, rate-limit partition isolation, and retry
  release;
- exact datetime fixtures, including 1–9 fractional digits and source-string
  round-trip.

The existing M1b synchronous `Transport::send()` seam remains valid and is not
replaced by this ADR. It is a low-level fake-friendly seam, not the application
async implementation.

## Open questions

- Which event-hub upstream release will include or document complete hub
  deadline aggregation?
- What default queue bounds and fairness policy are appropriate for one-shot
  operations versus streams?
- Which transport adapter API can expose response headers in wire order while
  retaining kurlyk's case-insensitive lookup convenience?
- Which official AT Protocol OAuth profile and fixture revision will gate M4?
