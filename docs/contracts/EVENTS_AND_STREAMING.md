# Events and streaming contract

## Responsibility

Offer an optional stream source (including Jetstream-compatible sources) and a
typed consumer lifecycle for public events. It is not a DM or private
notification transport.

## Inputs and outputs

Inputs include source URL, filters, cursor/checkpoint, deduplication key policy,
buffer limits, and shutdown token. Outputs are typed events, cursor updates,
and terminal status. Unknown event variants are observable without unsafe casts.

## Guarantees and errors

Reconnect uses bounded backoff and honors server hints. Delivery is at-least-once:
duplicates and replay after a checkpoint are expected. Ordering is guaranteed
only within the source scope documented by the adapter. Backpressure is
observable; the implementation may pause, bound, or reject according to policy.

## Concurrency and lifetime

One stream has one lifecycle owner; callbacks are serialized or explicitly
parallelized. A checkpoint is persisted only after the consumer's chosen
acknowledgement point. Graceful shutdown stops intake, drains within a deadline,
then reports dropped work explicitly.

## Integration boundary

WebSocket/HTTP stream implementations and event-hub-cpp adapters are optional.
The core stream contract does not require a particular run loop.
