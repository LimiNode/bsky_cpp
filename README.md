# bsky_cpp

**Modern C++17 Client SDK for Bluesky & AT Protocol.**

`bsky_cpp` is a general-purpose, asynchronous client library. It is not a UI
framework and it does not contain AI/LLM character logic. The M0 milestones
provide the C++17 scaffold, `Result`, typed `Error`, cooperative cancellation,
and the documented async contract. M1a adds strict value types for AT Protocol
identifiers; M1b adds the transport-neutral request/response seam and fake
transport; M1c adds an opt-in event-hub-backed operation runtime. The network
client and XRPC implementation remain deferred.

## Documentation

- [Architecture](docs/ARCHITECTURE.md) and [invariants](docs/INVARIANTS.md)
- [Glossary](docs/GLOSSARY.md)
- [Roadmap](docs/ROADMAP.md) and [implementation status](docs/IMPLEMENTATION_STATUS.md)
- [API contracts](docs/contracts/CLIENT_API.md)
- [Identifier contract](docs/contracts/IDENTIFIERS.md)
- [Engineering guides](docs/guides/coding-style.md)
- [Codebase orientation](docs/guides/codebase-orientation.md)
- [ADR-0001](docs/adr/0001-client-architecture.md)
- [ADR-0002](docs/adr/0002-async-execution-contract.md)
- [ADR-0005](docs/adr/0005-executor-abstraction.md)
- [ADR-0006](docs/adr/0006-operation-deadlines.md)
- [ADR-0007](docs/adr/0007-blocking-transport-bridge.md)
- [Reference scenarios](docs/reference-scenarios.md)

The canonical documentation language is English. M0c examples remain
conceptual; the implemented runtime is selected with `BSKY_BUILD_RUNTIME=ON`.

The merged partial M1c runtime currently runs non-blocking `Result<void>` work,
supports
callback-first typed `Result<T>` operations, and provides an opt-in bounded
blocking transport bridge. Move-only payloads whose `Result<T>` move is
`noexcept`, the shared `IExecutor` callback dispatch seam, cooperative
`steady_clock` deadlines, and blocking bridge ownership/lifecycle are documented
in
[OPERATION_RUNTIME.md](docs/contracts/OPERATION_RUNTIME.md).

## Includes

For the complete public surface:

```cpp
#include <bsky_cpp.hpp>
```

For a focused dependency, include the owning domain directly:

```cpp
#include <bsky_cpp/atproto/identifiers.hpp>
```

Public headers are installed from the explicit CMake allowlist. Private
domain-local `detail/` headers are not part of the SDK.

## Scope and status

D0 establishes boundaries, typed asynchronous contracts, identity and
authorization rules, delivery/lifetime semantics, persistence seams, and a
dependency-gated implementation plan. M0c fixes the async execution contract
before protocol implementation. See
[`IMPLEMENTATION_STATUS.md`](docs/IMPLEMENTATION_STATUS.md) for the evidence
behind every status.

## License

See [LICENSE](LICENSE).
