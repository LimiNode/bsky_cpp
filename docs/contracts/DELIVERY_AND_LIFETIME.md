# Delivery, cancellation, and lifetime contract

## Responsibility

Define how asynchronous operations start, complete, cancel, time out, retry,
and shut down across all SDK layers.

## Operation admission and outputs

Every call accepts an operation policy containing cancellation token, deadline,
retry policy, concurrency class, and completion executor. It returns one final
typed result and optional progress/diagnostic events that are safe to retain.
Local validation happens before admission. An invalid request fails
synchronously and creates no operation. An accepted operation owns its input
values and remains valid after the caller destroys the request object.

## Guarantees and errors

An accepted operation reaches exactly one internal terminal outcome and is
distinct from callback delivery. A cancellation request stops work that has not
started where possible; it does not forcibly interrupt transport code or a user
callback already executing. If the request wins the terminal-completion race,
the result is `Cancelled`; a completion that won first is delivered unchanged.
Deadline expiry is `TimedOut`; client shutdown is `Shutdown`. Repeated
cancellation requests are idempotent.

Retries consume a bounded budget and are permitted only by the operation-specific
retry policy in the relevant contract. A generic idempotency key is not a
protocol guarantee. When an uncertain side effect has no supported
deduplication, the operation finishes as `OutcomeUnknown` (with reconciliation
metadata) rather than issuing a blind retry. No callback is invoked synchronously
from a method unless the method explicitly says so.

## Callback registration, executor, and quiescence

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

The executor is an injected lifetime owner for queued and running callback
dispatch. Destroying an ordinary operation handle does not cancel the operation;
an explicit cancellation scope is required for cancellation-on-scope-exit.
Move-only callbacks are supported without requiring a copyable
`std::function`.

## Concurrency and shutdown lifetime

The client owns in-flight state through terminal completion and callback
quiescence. Shutdown is idempotent, rejects new work after its acceptance
boundary, stops intake, and then either drains or aborts active operations under
an explicit deadline. A successful shutdown result (`Quiesced`) includes the
callback-quiescence guarantee. If the deadline expires first, shutdown returns
an explicit `QuiescencePending`/`ShutdownTimedOut` result and retains the
operation-owned state and callback registrations needed by active callbacks;
it must not destroy that state to make the deadline appear successful. A later
quiescence event may complete cleanup. Shutdown never promises to forcibly stop
user code.

## Integration boundary

Cancellation, clocks, executors, and rate limiters are interfaces. A transport
may abort a socket, but the public contract remains transport-independent.
Queue limits and backpressure policy are operation-specific. A bounded queue
must report a typed backpressure outcome rather than silently dropping work or
allocating without limit. The conformance cases are listed in
[`ASYNC_CONFORMANCE.md`](ASYNC_CONFORMANCE.md).
