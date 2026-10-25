# Async execution conformance plan

This plan describes tests for a future implementation of the M0c contract. The
examples are behavioral requirements, not evidence that asynchronous machinery
already exists in the repository.

## Admission and completion

- Invalid input fails synchronously without creating an operation.
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
  observed.
- A callback already running is allowed to finish, and quiescence can be
  awaited without destroying captured state prematurely.
- A callback cannot synchronously wait for its own quiescence.
- The executor outlives every queued or running callback that references it.
- Callback exceptions are contained by the adapter policy and cannot create a
  second terminal outcome.
- Move-only callbacks work without requiring a copyable `std::function`.

## Handles, shutdown, and backpressure

- Destroying an ordinary `OperationHandle` does not implicitly cancel work.
- An explicit cancellation scope requests cancellation at scope exit.
- Shutdown rejects new admissions, settles existing operations under its
  deadline, and reports `QuiescencePending`/`ShutdownTimedOut` if callbacks
  remain active.
- Queue limits produce a typed backpressure outcome rather than unbounded
  allocation or silent loss.
- Destroying `Client` cannot produce use-after-free in transport, executor, or
  callback state.

## Test harness requirements

Use deterministic fake transports and executors for race coverage. Every
multi-threaded test has bounded waits and a CTest timeout. Run the conformance
matrix in Debug, Release, and sanitizer configurations; include an installed
external consumer for the public operation headers once those headers exist.
