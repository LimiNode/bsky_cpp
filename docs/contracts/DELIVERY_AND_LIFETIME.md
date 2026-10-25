# Delivery, cancellation, and lifetime contract

## Responsibility

Define how asynchronous operations start, complete, cancel, time out, retry,
and shut down across all SDK layers.

## Inputs and outputs

Every call accepts an operation policy containing cancellation token, deadline,
retry policy, concurrency class, and completion executor. It returns one final
typed result and optional progress/diagnostic events that are safe to retain.

## Guarantees and errors

Completion is at-most-once. Cancellation stops future work where possible and
returns `Cancelled`; a race with a completed operation returns the already
completed result. Deadline expiry is `TimedOut`; process/client shutdown is
`Shutdown`. Retries require idempotency and consume a bounded budget. No callback
is invoked synchronously from a method unless the method explicitly says so.

## Concurrency and lifetime

The client owns in-flight state through completion. Callback registrations are
weakly or token-owned and can be removed safely. Executors must not be blocked
by user callbacks; callback exceptions are captured by the adapter's policy.
Shutdown is idempotent and establishes a clear point after which new work is
rejected.

## Integration boundary

Cancellation, clocks, executors, and rate limiters are interfaces. A transport
may abort a socket, but the public contract remains transport-independent.
