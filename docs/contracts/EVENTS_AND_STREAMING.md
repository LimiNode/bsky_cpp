# Events and streaming contract

## Responsibility

Offer an optional stream source (including Jetstream-compatible sources) and a
typed consumer lifecycle for public events. It is not a DM or private
notification transport.

## Inputs and outputs

Inputs include source URL, filters, cursor/checkpoint, deduplication key policy,
buffer limits, delivery mode, and shutdown token. Outputs are typed events,
cursor updates, gap/drop notices, and terminal status. Unknown event variants
are observable without unsafe casts. The adapter exposes source capabilities,
including whether replay is available and which cursor format/generation it
uses; Jetstream-compatible v1/v2 behavior is not assumed to be identical.

## Guarantees and errors

Reconnect uses bounded backoff and honors server hints. In `Replayable` mode,
with a valid cursor and non-lossy queue, delivery is at-least-once: duplicates
and replay after a checkpoint are expected. In `LiveOnly` or explicitly
`Lossy` mode, delivery is best-effort and every dropped range is reported.
Ordering is guaranteed only within the source scope documented by the adapter.
Backpressure is observable; the implementation may pause, bound, or reject
according to policy, but it may not silently drop events in `Replayable` mode.

Cursor expiry or an unavailable replay window is a terminal/recovery outcome,
not an implicit cursor jump. The typed outcomes include `CursorTooOld`,
`CursorExpired`, `ReplayGap`, `UnsupportedReplay`, `BackpressureOverflow`, and
`SourceDataLost`; the application must choose whether to resnapshot, start a
new cursor, or stop. A checkpoint is valid only for the source and filter set
that produced it.

## Concurrency and lifetime

One stream has one lifecycle owner; callbacks are serialized or explicitly
parallelized. A checkpoint is persisted only after the consumer's chosen
acknowledgement point. Graceful shutdown stops intake, drains within a deadline,
then reports dropped work explicitly. It cannot forcibly interrupt a user
callback; callback quiescence follows the delivery/lifetime contract.

## Integration boundary

WebSocket/HTTP stream implementations and event-hub-cpp adapters are optional.
The core stream contract does not require a particular run loop.
