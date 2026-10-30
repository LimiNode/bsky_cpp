# ADR-0002: Async execution contract

- **Status:** Accepted for M0c; implementation remains deferred.
- **Date:** 2026-10-25
- **Depends on:** ADR-0001 and `DELIVERY_AND_LIFETIME.md`

## Context

The public client is asynchronous, but C++17 does not provide one abstraction
that combines cooperative cancellation, callback dispatch, operation lifetime,
and composition. Choosing a concrete transport or futures framework before
settling those semantics would make every later API depend on accidental
threading and ownership rules.

The contract must work with an injected `kurlyk` adapter, an in-process fake
transport, or another executor without exposing any of those implementations
in public DTOs.

## Options considered

### `std::future` as the primary API

This is familiar and useful for one-shot blocking adapters, but C++17 futures
do not carry cooperative cancellation, callback-executor policy, or a clear
way to close callback registration and await quiescence. A future-only API also
encourages blocking at integration boundaries.

### Callback-first without an operation handle

Callbacks map naturally to event-driven clients, but callers need an explicit
way to request cancellation, inspect terminal state, and coordinate shutdown.
Keeping that state implicit makes ownership and races difficult to test.

### Callback-first with a handle and injected executor

This keeps delivery non-blocking while making operation state and cancellation
explicit. It supports move-only callables, deterministic fake executors, and an
optional future adapter without making a futures library part of the core.

## Decision

Use a callback-first operation contract with these conceptual components:

- `Result<T>` is the exactly-one terminal outcome;
- `OperationHandle` owns an observation/control reference and exposes explicit
  `request_cancellation()`, state inspection, and a completion/quiescence wait;
- `CancellationToken` is the cooperative cancellation input;
- `IExecutor` is injected and owns callback dispatch, not operation state;
- callback registration is separately closable and may be awaited for
  quiescence;
- a `std::future` adapter is optional and must not change cancellation or
  lifetime semantics.

Admission returns `Result<OperationHandle>`. A failure is synchronous and means
that no operation was admitted and no callback may be invoked. A successful
result admits the operation; its callback (or selected future adapter) receives
the one terminal payload asynchronously.

Destroying an ordinary `OperationHandle` releases that observer/control
reference; it does not implicitly cancel the network operation. A separate
RAII cancellation scope may request cancellation on scope exit when an
application explicitly chooses that policy.

An accepted operation has one internal terminal outcome, regardless of whether
its callback is delivered, suppressed, or throws. Completion and cancellation
use a first-winner rule: cancellation wins only if it reaches the operation
before terminal completion. Callback delivery is never synchronous from an
API method unless that method explicitly documents the exception.

The handle owns control and status observation, not an implicit second copy of
the typed payload. Callback delivery and the optional future adapter are
mutually exclusive one-shot delivery channels for a move-only result.

The client normally owns an executor lifetime lease (for example, a shared
executor object) and each accepted operation retains that lease through
terminal completion and callback quiescence. A borrowed executor requires an
explicit adapter and a caller-provided lifetime guarantee. Rejection before
admission is a synchronous `Result` failure; rejection after admission becomes
one typed terminal dispatch error. Implementations must not silently run a
callback synchronously as a fallback. A stopping executor must either reject
queued callbacks through that policy or drain them under its documented rules.

The M1b `Transport::send()` interface is deliberately a low-level synchronous
seam for owned request/response values and deterministic fakes. It is not the
application-facing async API and must not be called directly from an event or
callback thread. M1c supplies the bounded-worker bridge for blocking
implementations; a native-async adapter may integrate behind the same
operation state machine without changing the public ownership contract.

Client destruction stops admission and initiates shutdown. Operation state and
executor leases are shared independently of the `Client` object, so a
`QuiescencePending`/`ShutdownTimedOut` result can safely outlive the client
without retaining pointers into destroyed client storage.

For callback registration, a queued callback checks registration state when the
executor starts it. Closing before that check suppresses delivery; a callback
that has started is in-flight and may finish. Quiescence means no in-flight
callback remains and every queued registration is delivered or suppressed.

## Required guarantees

Implementations must define and test:

1. admission and synchronous validation failures;
2. exactly-one terminal outcome;
3. callback delivery, suppression, and callback exception handling;
4. executor ownership and dispatch guarantees;
5. cancellation/completion races;
6. handle ownership and destructor semantics;
7. shutdown and callback quiescence;
8. timeout and operation-specific retry boundaries;
9. bounded queues and backpressure outcomes;
10. client destruction and rejection of new work;
11. exception containment at transport and callback boundaries;
12. optional future adaptation without changing the core contract.

Move-only callbacks are supported by the conceptual contract; an implementation
must not require copyable `std::function` when a move-only callable is suitable.

## Consequences

The core remains C++17 and transport-independent. Implementations need a small
state machine and explicit executor lifetime rather than a general futures
framework. Applications must keep callbacks short, retain the client/executor
for the documented lifetime, and handle `Cancelled`, `TimedOut`, `Shutdown`,
and `OutcomeUnknown` distinctly.

No transport, XRPC, executor implementation, or callback machinery is added by
M0c. Those remain later implementation work and must conform to this ADR.

## Open questions

- Which concrete executor adapters should be supported first?
- Should the optional future adapter expose a blocking `get()` or only a bridge
  to an application-owned future type?
- Which queue and backpressure defaults are appropriate for streaming versus
  one-shot operations?
