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

## M1b transport seam

M1b exposes owned `TransportRequest` and `TransportResponse` values, a
transport-neutral synchronous `Transport` interface, and a deterministic
`FakeTransport` for tests. Requests accept only absolute HTTP(S) URLs, reject
credential-bearing authorities and header control characters, and own their
body bytes. URL validation is syntactic only: it checks DNS-style hosts,
bracketed IPv6 literals, decimal ports, and percent escapes without DNS
resolution or an SSRF allowlist. HTTP URL fragments are rejected because they
are not request-target data. Headers are ASCII-token names with bounded count
and field sizes; request `set_header()` replaces a name, while
`append_header()` preserves repeated response fields such as `Set-Cookie`.
Cancellation is observed before adapter dispatch. The fake transport contains
handler exceptions as generic typed transport errors without copying exception
text into diagnostics.

The synchronous `Transport::send()` is an explicitly limited blocking seam,
not the SDK's application-facing async API and not a required implementation
path for native-async adapters. M1c must invoke blocking implementations through
a bounded worker bridge and keep event/callback threads non-blocking. A future
`kurlyk` adapter may deliver completion directly into the operation runtime,
without `future.get()` and without a per-request waiter thread, while preserving
request/response ownership. Async admission, executor ownership, shutdown, and
callback delivery remain M1c responsibilities. Body size limits, streaming, and
backpressure are owned by the concrete adapter and its M1c integration rather
than by a global transport value limit.
