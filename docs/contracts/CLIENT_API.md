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
// Conceptual; not implemented in D0.
Async<Result<Post>> create_post(CreatePostRequest request, OperationOptions options);
```

## Guarantees and errors

Local validation fails before network I/O. Errors distinguish invalid input,
identity/discovery, authentication, authorization, transport, protocol,
decoding, rate limit, timeout, cancellation, and shutdown. Server error bodies
are bounded and secrets are redacted.

## Concurrency and lifetime

Calls may run concurrently subject to an explicit limiter. Request values are
owned by the operation; callers may destroy inputs after submission. Callbacks
run on the documented executor and must not block. The client remains alive
until operations complete or are cancelled; shutdown rejects new work and
settles existing work according to `DELIVERY_AND_LIFETIME.md`.

## Integration boundary

The client depends on abstract transport, clock, executor, credential, and
optional persistence interfaces. No kurlyk or event-hub type appears in a public
high-level DTO.
