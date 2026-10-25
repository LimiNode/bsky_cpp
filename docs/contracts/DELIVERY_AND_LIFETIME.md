# Delivery, cancellation, and lifetime contract

## Responsibility

Define how asynchronous operations start, complete, cancel, time out, retry,
and shut down across all SDK layers.

## Inputs and outputs

Every call accepts an operation policy containing cancellation token, deadline,
retry policy, concurrency class, and completion executor. It returns one final
typed result and optional progress/diagnostic events that are safe to retain.

## Guarantees and errors

Completion is at-most-once and is distinct from a cancellation request. A
cancellation request stops work that has not started where possible; it does
not forcibly interrupt transport code or a user callback already executing. If
the request wins the terminal-completion race, the result is `Cancelled`; a
completion that won first is delivered unchanged. Deadline expiry is `TimedOut`;
client shutdown is `Shutdown`.

Retries consume a bounded budget and are permitted only by the operation-specific
retry policy in the relevant contract. A generic idempotency key is not a
protocol guarantee. When an uncertain side effect has no supported
deduplication, the operation finishes as `OutcomeUnknown` (with reconciliation
metadata) rather than issuing a blind retry. No callback is invoked synchronously
from a method unless the method explicitly says so.

## Callback registration and quiescence

Operation completion and callback registration have independent lifetimes. A
registration token has `close`/unregister semantics: after close is observed,
no new callback invocation is scheduled, but an invocation already in progress
may finish. An explicit `await_quiescence` (or an equivalent completion event)
is the synchronization point after which captured state can be destroyed. A
callback may not synchronously wait for its own quiescence; self-unregister is
deferred until that invocation returns.

The SDK documents the selected executor and does not claim it can preempt or
make an arbitrary user callback non-blocking. Applications must keep callbacks
short or dispatch work elsewhere. Callback exceptions are captured according to
the adapter policy and cannot bypass terminal completion.

## Concurrency and shutdown lifetime

The client owns in-flight state through terminal completion and callback
quiescence. Shutdown is idempotent, rejects new work after its acceptance
boundary, stops intake, and then either drains or aborts active operations under
an explicit deadline. Shutdown completion includes the documented callback
quiescence guarantee; it never promises to forcibly stop user code.

## Integration boundary

Cancellation, clocks, executors, and rate limiters are interfaces. A transport
may abort a socket, but the public contract remains transport-independent.
