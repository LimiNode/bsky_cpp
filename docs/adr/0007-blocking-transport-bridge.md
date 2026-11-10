# ADR-0007: Bounded blocking transport bridge

- **Status:** Accepted for the merged M1c runtime subset; broader M1c remains partial
- **Date:** 2026-11-08
- **Depends on:** [ADR-0002](0002-async-execution-contract.md),
  [ADR-0004](0004-runtime-composition.md),
  [ADR-0005](0005-executor-abstraction.md), and
  [ADR-0006](0006-operation-deadlines.md)

## Context

M1b exposes a transport-neutral synchronous boundary:
`Transport::send(const TransportRequest&, CancellationToken)`. Calling that
function from the event-hub loop or a callback executor would block deadlines,
other operations, and shutdown. The first bridge therefore needs a bounded
worker lane while keeping the existing operation state machine and callback
contract.

## Decision

`Runtime::submit_blocking()` is the explicit bridge API. It accepts a shared
transport lifetime lease, an owned `TransportRequest`, an optional typed
`Result<TransportResponse>` callback, and the existing `OperationOptions`.
The response is delivered through the same `OperationHandle`, first-winner,
callback-registration, injected-`IExecutor`, and quiescence machinery as other
typed operations.

When `RuntimeOptions::max_blocking_in_flight` is non-zero, the runtime registers
one `event_hub::Module` in `private_thread` mode before startup. The
`ModuleHub` owns that module, its `TaskManager`, worker lifecycle, and shutdown;
the bridge does not create another `TaskManager`, `EventBus`, general-purpose
scheduler, or per-request thread. The lane has exactly one worker, so blocking
calls are serialized. The operation admission counter bounds both queued and
running blocking jobs. A full lane returns `Backpressure` synchronously and
does not invoke a callback.

The blocking worker owns the transport lease, request buffers, and operation
state until `send()` returns. Cancellation, deadline, and shutdown commit the
cooperative runtime outcome and request cancellation, but never forcibly
interrupt a running transport call. A later transport response cannot rewrite
the committed outcome. If a call has already reached the transport, a
`Cancelled` or `TimedOut` result does not prove that a remote side effect did
not happen; the bridge performs no automatic retries and does not claim
idempotency.

Admission failure before registration invokes no callback. A worker queue
rejection after admission becomes one `Shutdown` terminal outcome. A transport
exception becomes `Internal`; callback exceptions remain contained by the
existing runtime. Executor rejection or accepted-task destruction suppresses
delivery without changing the already committed outcome.

When the final admitted operation retires during shutdown, the runtime records
the need for a hub wake-up while holding its state mutex, releases that mutex,
and only then calls `ModuleHub::request_stop()`. This keeps notifier and private
worker wake-up code outside the runtime critical section and preserves the
existing external-finalization lock order.

The bridge is opt-in. `max_blocking_in_flight == 0` keeps the runtime's existing
non-blocking composition and creates no private worker. The current pre-1.0
compatibility promise is source compatibility for existing `RuntimeOptions`
aggregate initializers and submit calls; binary compatibility is not promised.

## Consequences

- The hub loop remains responsive while one blocking call is in progress; timer
  dispatch and other non-blocking work use the original runtime lane.
- Worker count and queued work are bounded by construction. A single worker is
  intentionally conservative; a future multi-worker policy needs a separate
  review because it changes fairness, ordering, and resource limits.
- Shutdown waits for the blocking call only within its normal timeout policy.
  A blocked transport can therefore produce `ShutdownTimedOut`; releasing or
  otherwise cooperating with the transport is the application's responsibility.
- `kurlyk`, native asynchronous adapters, XRPC mapping, retries, and high-level
  client behavior remain outside this bridge.
