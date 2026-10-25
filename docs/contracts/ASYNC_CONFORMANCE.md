# Async execution conformance plan

This plan describes tests for a future implementation of the M0c contract. The
examples are behavioral requirements, not evidence that asynchronous machinery
already exists in the repository.

## Admission and completion

- Invalid input fails synchronously without creating an operation.
- The conceptual admission result is `Result<OperationHandle>`; an admission
  failure never invokes the callback.
- Accepted work produces exactly one internal terminal `Result<T>`.
- A callback receives at most one delivery, and callback suppression does not
  change the terminal outcome.
- Callback delivery is asynchronous relative to the initiating method unless a
  method explicitly documents synchronous completion.

## Cancellation, timeout, and races

- Cancellation before work starts produces `Cancelled` and prevents work where
  the transport permits it.
- Completion that wins the race remains the terminal result even if cancellation
  is requested immediately afterward.
- Deadline expiry produces `TimedOut`; client shutdown produces `Shutdown`.
- Repeated cancellation requests are idempotent and safe from concurrent
  requesters.
- An uncertain side effect is not blindly retried; it reports reconciliation
  metadata or `OutcomeUnknown` according to its operation policy.

## Callback and executor lifetime

- A registration can be closed; no new callback is scheduled after closure is
  observed. A callback already queued checks registration state before starting.
- The race between queueing, registration close, executor start, and quiescence
  is deterministic: close-before-start suppresses delivery; once started, the
  callback is in-flight and may finish.
- A callback already running is allowed to finish, and quiescence can be
  awaited without destroying captured state prematurely.
- A callback cannot synchronously wait for its own quiescence.
- The executor outlives every queued or running callback that references it.
- An executor lease is retained through terminal completion and callback
  quiescence. A borrowed executor requires an explicit lifetime guarantee.
- Rejection before admission is a synchronous failure; rejection after
  admission becomes one typed terminal dispatch error. No synchronous fallback
  callback is allowed. Executor shutdown must either reject queued callbacks or
  drain them according to its documented policy.
- Callback exceptions are contained by the adapter policy and cannot create a
  second terminal outcome.
- Move-only callbacks work without requiring a copyable `std::function`.

## Handles, shutdown, and backpressure

- Destroying an ordinary `OperationHandle` does not implicitly cancel work.
- An explicit cancellation scope requests cancellation at scope exit.
- The handle observes and controls operation state; it does not promise a second
  copy of a move-only payload. Callback and future delivery are mutually
  exclusive one-shot channels.
- Shutdown rejects new admissions, settles existing operations under its
  deadline, and reports `QuiescencePending`/`ShutdownTimedOut` if callbacks
  remain active.
- Queue limits produce a typed backpressure outcome rather than unbounded
  allocation or silent loss.
- Destroying `Client` cannot produce use-after-free in transport, executor, or
  callback state. Shared operation state and executor leases remain valid after
  the client object is gone.

## Test harness requirements

Use deterministic fake transports and executors for race coverage. Every
multi-threaded test has bounded waits and a CTest timeout. Run the conformance
matrix in Debug, Release, and sanitizer configurations; include an installed
external consumer for the public operation headers once those headers exist.
