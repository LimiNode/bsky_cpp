# Architecture

## Purpose and boundaries

`bsky_cpp` is a layered client SDK. It models AT Protocol identities and
records, translates typed application operations to XRPC, and exposes
asynchronous results. It does not render UI, implement an LLM, own an event
loop, or promise server-side semantics that the protocol does not provide.

## Layers

1. **Value and protocol primitives** — UTF-8 text, DID, Handle, AT URI, CID,
   StrongRef, Lexicon values, typed errors, cancellation, and timeouts.
2. **Transport boundary** — an injectable HTTP/XRPC and optional WebSocket
   adapter. The boundary owns wire details, status mapping, retry hints, and
   response bytes; it does not define high-level DTOs.
3. **Identity and authorization** — service discovery, PDS/AppView/Chat
   endpoint selection, App Password sessions, and AT Protocol OAuth.
4. **Low-level AT Protocol API** — arbitrary XRPC queries/procedures, record
   operations, repository primitives, and AT URI/CID handling.
5. **High-level client API** — typed posts, threads, media, social graph,
   profiles, notifications, and chat operations. It is a convenience layer,
   never the only route to an operation.
6. **Optional infrastructure** — event streaming, persistence, and event-hub
   integration behind interfaces. The core remains usable in memory.

Dependencies point downward. High-level code depends on protocol contracts and
interfaces, never on a concrete HTTP library, MDBX, or an application runtime.

## Source and public-header layout

Public source files remain colocated with their implementation files under
`src/bsky_cpp/`, organized by ownership domain:

- `core/` — `Error`, `Result`, cancellation, and version primitives;
- `atproto/` — DID, Handle, NSID, AT URI, CID, and StrongRef values;
- `transport/` — HTTP request/response values and transport seams.

`src/bsky_cpp.hpp` is the canonical umbrella include and exports only these
stable public domains. Runtime, XRPC, identity, auth, data, client, streams,
and adapter directories are future boundaries; they are not created as empty
placeholders. Private implementation headers, when needed, belong in a local
domain `detail/` directory and are not installed.

The Stage 1 migration changes direct include paths as follows:

| Previous include | Canonical include |
| --- | --- |
| `bsky_cpp/error.hpp` | `bsky_cpp/core/bsky_error.hpp` |
| `bsky_cpp/result.hpp` | `bsky_cpp/core/result.hpp` |
| `bsky_cpp/cancellation.hpp` | `bsky_cpp/core/cancellation.hpp` |
| `bsky_cpp/version.hpp` | `bsky_cpp/core/version.hpp` |
| `bsky_cpp/identifiers.hpp` | `bsky_cpp/atproto/identifiers.hpp` |
| `bsky_cpp/transport.hpp` | `bsky_cpp/transport/transport.hpp` |

The SDK is pre-1.0 and has no supported external include compatibility promise;
therefore no duplicate forwarding headers are installed. C++ names, namespaces,
runtime behavior, and ownership contracts are unchanged by this file-layout
migration.

## Request lifecycle

An operation validates local invariants, resolves an identity and endpoint,
constructs a typed XRPC request, submits it through the transport adapter, maps
the response or protocol error, and completes exactly once. Cancellation and
shutdown may produce a typed terminal cancellation/shutdown error. A callback
registration is separate from terminal completion: closing it prevents future
dispatch, while callback quiescence is the point at which captured state may be
destroyed. The terminal outcome remains retained until all operation-owned
resources and active callback invocations are quiescent.

## Async model

The primary surface is asynchronous. Implementations may use a future,
callback, or sender-like adapter, but each operation must define completion,
executor/dispatch policy, cancellation, timeout, backpressure, retry, and
thread-safety semantics. A synchronous facade, if added, is an explicit CLI
convenience and must not block event callbacks.

## Extensibility

New Lexicons and media types are added behind protocol/value interfaces. New
transports, persistence stores, and stream sources implement seams without
changing public DTOs. Unknown server fields are retained or safely ignored
according to the operation contract so forward-compatible responses do not
break clients.

## Security posture

All external URLs and discovered services are treated as untrusted input.
Discovery and authenticated proxy requests enforce scheme/host policy,
identity verification, token audience, and redirect rules. Secrets are
redacted, scoped, refreshable, and never written to ordinary logs.

## Reference inputs

This foundation was checked against the current public AT Protocol XRPC,
Lexicon, and OAuth specifications, Bluesky API documentation, and the
`bluesky-social/atproto` source tree. Engineering conventions were compared
with the public `mdbx-containers`, `kurlyk`, and `event-hub-cpp` repositories.
Those projects inform style and adapter seams; their domain architectures are
not copied into this SDK. External references are informative and must be
revalidated when a milestone pins a protocol behavior.
