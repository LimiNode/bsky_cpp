# AT Protocol and Lexicon contract

## Responsibility

This layer represents XRPC queries/procedures, records, repositories, and
protocol primitives without pretending that every Lexicon is known at compile
time.

## Inputs and outputs

An operation name, validated parameters, optional body, endpoint, auth context,
and timeout produce a typed response or a bounded protocol error. Unknown fields
are retained as an explicit extension value where forward compatibility matters.
AT URI, CID, DID, Handle, and StrongRef constructors reject malformed input.
The low-level request also records the operation's retry-safety class.

## Guarantees and errors

Encoding and content negotiation follow the current Lexicon/XRPC operation;
XRPC is not reduced to one universal JSON/CBOR shape because some operations
use binary or multipart payloads. HTTP status, XRPC error name, retry hints,
and decode failures remain distinguishable. A query may be retried when the
operation is safe. A side-effecting procedure requires protocol-defined
deduplication or an adapter contract that explicitly guarantees the caller's
deterministic identity is honored. A deterministic value without such a
protocol/adapter guarantee is not sufficient.

For `com.atproto.repo.createRecord`, a caller may select a deterministic `rkey`
and retry the same collection/rkey/body. A conflict is not automatically a
failure or success: the client performs read-back reconciliation and compares
the existing record/CID before reporting `Created`, `AlreadyExists`, or
`OutcomeUnknown`. If no deterministic rkey or other supported deduplication is
available, an uncertain result is not blindly retried. This rule also applies
to `chat.bsky.convo.sendMessage`, which has no universal client idempotency key.

## Concurrency and lifetime

Transport handles are shareable only if documented thread-safe. Decoded values
own their data and outlive the response buffer. A parser never returns views
into a destroyed request/response buffer.

## Integration boundary

The layer is independent of concrete HTTP/WebSocket libraries. Generated
Lexicon code is a future build artifact, not part of D0.
