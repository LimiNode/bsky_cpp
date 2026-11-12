# ADR-0008: Native transport and XRPC integration boundary

- **Status:** Draft M1d0 contract
- **Date:** 2026-11-12
- **Depends on:** [ADR-0002](0002-async-execution-contract.md),
  [ADR-0004](0004-runtime-composition.md),
  [ADR-0007](0007-blocking-transport-bridge.md), and the
  [M1b transport contract](../contracts/ATPROTO_AND_LEXICONS.md)

## Context

M1c implements the operation state machine and an explicit bridge for the
existing synchronous `Transport::send()` seam. M1d needs a native asynchronous
transport adapter that forwards a network completion into that state machine
without blocking an event-hub loop, a callback executor, or a kurlyk worker.

The audited kurlyk snapshot owns a process-level `NetworkWorker`, per-client
admission, cancellation groups, retry configuration, rate-limit handles, and
HTTP response diagnostics. The bsky SDK must reuse those facilities at an
adapter boundary without leaking kurlyk types into the foundation or creating a
second scheduler. The source-backed reuse decisions are recorded in
[`DEPENDENCY_REUSE_AUDIT.md`](../research/DEPENDENCY_REUSE_AUDIT.md).

This ADR freezes the integration boundary only. It does not claim that an
async adapter, XRPC codec, high-level Client, OAuth, or network operation is
implemented.

## Decision

### Boundary and ownership

- `Transport::send()` and `Runtime::submit_blocking()` remain the synchronous
  compatibility path. Native asynchronous operations must not route through
  that blocking bridge.
- The future adapter introduces one transport-neutral completion seam owned by
  the runtime/client integration. A concrete kurlyk adapter is optional and
  lives in an opt-in `bsky_cpp::kurlyk_adapter` component; foundation targets
  never discover kurlyk.
- Request bytes, response bytes, headers, and diagnostic strings are owned by
  the operation or adapter until terminal forwarding completes. The adapter
  must not pass views into a request object or a kurlyk callback buffer whose
  lifetime has ended.
- The runtime retains an explicit lease for the network client and the
  process-level kurlyk worker. A kurlyk callback must not destroy the last
  `HttpClient`, stop the shared worker, or synchronously wait for group
  cancellation. Client destruction and final worker shutdown are performed by
  an owner outside the worker callback.

### Admission, completion, and cancellation

- Local validation and transport admission failures are synchronous
  `Result<OperationHandle>` failures and invoke no callback.
- Accepted native work forwards exactly one completion into the existing
  first-winner operation state. Callback placement, callback suppression,
  quiescence, typed result ownership, and executor rejection retain the
  semantics of ADR-0002 and `OPERATION_RUNTIME.md`.
- The adapter must forward native completion directly. It must not call
  `future.get()` from an event-loop or kurlyk worker callback and must not add
  a per-request waiter thread.
- Cancellation is cooperative. It requests cancellation from the adapter and
  commits the bsky terminal outcome when it wins the race; it cannot forcibly
  stop a request that has reached the network. A late response cannot rewrite
  the committed outcome, and a remote side effect may remain uncertain.
- The adapter reuses the existing runtime scheduler and executor. It creates
  no second `TaskManager`, `EventBus`, general-purpose scheduler, or client run
  loop. Kurlyk's singleton worker is shared according to its documented
  process-level lifetime policy.

### Error and response mapping

The adapter keeps these causes distinct until the bsky mapping layer has enough
context to classify them:

1. synchronous local validation or admission rejection;
2. transport failure or cancellation, including a missing HTTP status;
3. an HTTP terminal response and its status/headers;
4. an XRPC protocol error carried by an otherwise valid HTTP response; and
5. an uncertain remote side effect after cancellation, timeout, or connection
   loss.

Kurlyk synthetic statuses are diagnostics, not protocol success. The adapter
must inspect the transport error code and cancellation context before mapping a
synthetic timeout/cancellation status such as `499` or a curl-error status such
as `451`. Status alone must never turn a transport failure into an XRPC success.
The mapping must preserve secret-redaction rules from the core `Error` type.

Repeated response-header names and values remain distinct. The current kurlyk
header container does not promise original global field-line order, so bsky
must not advertise that stronger guarantee without a separately reviewed
upstream/raw-header decision.

### Retry, rate limits, and endpoint policy

- Kurlyk owns wire-level retry mechanics, rate-limit handles, sequential
  in-flight tokens, and release on completion. bsky does not build a second
  token bucket.
- bsky owns operation-specific retry safety and idempotency. Unsafe
  side-effecting procedures disable automatic retry unless protocol-validated
  idempotency is available. A retry policy must never hide an uncertain side
  effect.
- Endpoint selection, redirects, authentication headers, and rate-limit
  partition keys are explicit adapter/client policy. They are not inferred
  from arbitrary response text and are not part of foundation DTOs.

## M1d implementation slices

- **M1d0:** this contract, kurlyk API/lifetime review, dependency boundary, and
  deterministic test plan.
- **M1d1:** one native async adapter and fake transport that forward a single
  HTTP completion through the existing runtime. The transport-neutral slice is
  implemented by `Runtime::submit_async()`, `AsyncTransport`, and
  `FakeAsyncTransport`; kurlyk forwarding remains deferred.
- **M1d2:** basic typed XRPC query/procedure encoding and response decoding.
- **M1d3:** transport/HTTP/XRPC error mapping, retry/idempotency policy,
  rate-limit partitions, endpoint/redirect/auth rules, and security fixtures.
- **M1 closeout:** DID/Handle resolution, discovery, and the first public read
  scenario with wire fixtures and installed-consumer checks.

Every slice remains independently reviewable. No slice may add a second
scheduler or silently change `OperationHandle`, `IExecutor`, terminal outcome,
or foundation dependency semantics.

## Required conformance fixtures

The adapter test plan must include deterministic fake-network cases for:

- synchronous admission rejection and bounded in-flight capacity;
- one accepted terminal response and duplicate/late completion suppression;
- cancellation before dispatch, cancellation after dispatch, and timeout;
- transport failure, synthetic `499`, synthetic `451`, HTTP status failure,
  and XRPC error mapping;
- repeated headers such as `Set-Cookie` without comma joining;
- callback executor rejection, client destruction, worker-lifetime retention,
  and shutdown without blocking a worker callback;
- retry authorization for safe operations and retry suppression for unsafe
  side effects; and
- endpoint, redirect, authentication, secret-redaction, and rate-limit policy.

These fixtures are acceptance gates for M1d implementation PRs, not evidence
that this documentation-only ADR has implemented the adapter.

## Consequences

The existing M1c runtime remains usable without kurlyk and keeps its blocking
bridge explicit. Native async integration can reuse kurlyk's worker and policy
machinery while forwarding completion directly into the already-tested
operation state machine. The cost is an explicit client/network lease and a
separate mapping layer for transport, HTTP, and XRPC semantics.

## Open questions

- Which stable partition key represents a bsky account or service boundary for
  kurlyk rate limits?
- Which endpoint discovery and redirect policy should M1d0 expose before DID
  resolution is implemented?
- Does the selected kurlyk release provide sufficient raw-header information,
  or is an upstream ordered-header extension required?
- Which typed XRPC codec representation should be used for JSON, CBOR, binary,
  and multipart operations while preserving Lexicon source strings?
