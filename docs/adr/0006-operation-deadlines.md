# ADR-0006: Cooperative operation deadlines

- **Status:** Draft M1c implementation
- **Date:** 2026-11-07
- **Depends on:** [ADR-0002](0002-async-execution-contract.md),
  [ADR-0004](0004-runtime-composition.md), and
  [ADR-0005](0005-executor-abstraction.md)

## Context

The operation runtime needs a per-operation timeout without adding a second
scheduler or changing the first-winner and callback-lifetime guarantees. A
deadline is an operation policy, not a transport timeout and not the timeout
used while shutting down the runtime.

## Decision

`OperationOptions::deadline` is an optional absolute
`std::chrono::steady_clock::time_point`. The value is monotonic and is never
converted from or compared with wall-clock time. Existing two-argument
`submit(work, callback)` and `submit_typed(work, callback)` calls remain valid;
the options object is a trailing defaulted argument.

The runtime registers one delayed task with the existing event-hub
`TaskManager`. It does not create a timer thread, polling loop, event bus, or a
second scheduler. The operation state keeps only the task id needed to cancel
the registration after a terminal outcome. A registration failure is treated
as an admitted `Shutdown` outcome and still follows the normal callback
executor path.

The deadline task participates in the same first-winner race as success,
failure, explicit cancellation, and shutdown:

1. If it wins before terminal commitment, the runtime records
   `ErrorCategory::TimedOut`, requests cooperative cancellation, and dispatches
   the void or typed error through the selected callback executor.
2. If another terminal outcome wins first, the timeout is cancelled or made
   harmless and cannot rewrite the outcome or request cancellation afterwards.
3. A running user function is never forcibly interrupted. If it blocks past
   the deadline, the result is committed at the next scheduler dispatch point;
   a completion that commits before the timeout task runs wins normally.
4. Closing a callback registration or stopping the runtime suppresses queued
   timeout delivery exactly as it suppresses every other terminal callback.

The same task manager is used in managed and external modes. In external mode
the task manager's notifier wakes the embedding loop when a deadline is added,
cancelled, or becomes due; `next_deadline()` aggregates the operation timer
with other hub deadlines and `process_once()` performs the transition. In
managed mode the existing hub loop waits for and dispatches the timer.

## Consequences

- Deadline state shares the existing operation registry, capacity accounting,
  cancellation source, callback executor lease, and shutdown quiescence.
- No real-time preemption is promised. Applications must keep work and capture
  destructors short and cooperative.
- Operation deadlines, transport-level request timeouts, and
  `shutdown(timeout)` remain distinct policies and error paths.
- Tests use an already-expired deadline and bounded steady-clock waits for the
  upstream scheduler. State-machine races are checked without sleeps or a
  second timer implementation.

