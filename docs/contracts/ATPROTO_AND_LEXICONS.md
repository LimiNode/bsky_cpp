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

## Guarantees and errors

JSON/CBOR encoding follows the current Lexicon schema. HTTP status, XRPC error
name, retry hints, and decode failures remain distinguishable. A query may be
retried when safe; a procedure with side effects requires idempotency evidence.

## Concurrency and lifetime

Transport handles are shareable only if documented thread-safe. Decoded values
own their data and outlive the response buffer. A parser never returns views
into a destroyed request/response buffer.

## Integration boundary

The layer is independent of concrete HTTP/WebSocket libraries. Generated
Lexicon code is a future build artifact, not part of D0.
