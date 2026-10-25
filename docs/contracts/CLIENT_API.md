# Client API contract

## Responsibility

The client composes identity, authorization, protocol, and domain operations
into typed asynchronous calls. It owns validation and mapping, not transport
connections or application scheduling.

## Inputs and outputs

Inputs are immutable value types and explicit option objects. Outputs are typed
DTOs or a typed `Result` carrying one error category and diagnostic metadata
safe for logs. Low-level XRPC remains available for operations without a
high-level wrapper. Conceptual shape:

```cpp
// Conceptual; not implemented in M0c.
Result<OperationHandle> create_post(
    CreatePostRequest request,
    OperationOptions options,
    MoveOnlyCallback<Result<Post>> callback);
```

The callback-first form is the primary contract selected by
[`ADR-0002`](../adr/0002-async-execution-contract.md). A future adapter may be
provided for simple integrations, but it does not change cancellation,
completion, or lifetime semantics.

## Admission, guarantees, and errors

Local validation fails before operation admission and network I/O. An invalid
request returns a synchronous `Result<OperationHandle>::failure()` and creates
no operation or callback delivery. Errors distinguish invalid input,
identity/discovery, authentication, authorization, transport, protocol,
decoding, rate limit, timeout, cancellation, and shutdown. Server error bodies
are bounded and secrets are redacted.

## Concurrency and lifetime

Calls may run concurrently subject to an explicit limiter. Request values are
owned by the operation; callers may destroy inputs after submission. A successful
`Result<OperationHandle>` means the operation was admitted. The accepted
operation has exactly one internal terminal `Result<T>` even if its callback is
suppressed or throws. Callback delivery is asynchronous relative to the
initiating method unless a specific API documents otherwise.

Callbacks run on the documented injected executor and must not block. The
returned `OperationHandle` exposes explicit cancellation, state inspection, and
a completion/quiescence wait. Destroying an ordinary handle releases an observer
reference; it does not implicitly cancel the operation. An explicit RAII
cancellation scope may opt into cancellation-on-exit.

The handle is a control and observation object. It does not promise an
independent second copy of the typed payload, which may be move-only. A
callback registration and an optional future adapter are alternative terminal
delivery channels selected at admission; an operation does not deliver the
same move-only payload through both channels.

The client remains alive until operations complete or are cancelled; shutdown
rejects new work and settles existing work according to
`DELIVERY_AND_LIFETIME.md`. Callback registration is a separate lifetime: it
can be closed to suppress future delivery and then quiesced before captured
state is destroyed.

The executor outlives queued and running callbacks. The client retains an
executor lifetime lease (normally a `shared_ptr`-like ownership token) through
terminal completion and callback quiescence. A deliberately borrowed executor
is allowed only through an explicit adapter whose caller guarantees that
lifetime. If admission cannot enqueue work, `Result<OperationHandle>` fails
synchronously. If an already-admitted dispatch is rejected, the adapter records
one typed terminal dispatch error and never falls back to an unexpected
synchronous callback.

The client does not promise to preempt a callback or make arbitrary user code
non-blocking. Exceptions crossing transport or callback boundaries are mapped to
typed errors or contained by adapter policy; they cannot create a second
terminal outcome.

## Integration boundary

The client depends on abstract transport, clock, executor, credential, and
optional persistence interfaces. No kurlyk or event-hub type appears in a public
high-level DTO.
