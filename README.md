# bsky_cpp

**Modern C++17 Client SDK for Bluesky & AT Protocol.**

`bsky_cpp` is a general-purpose, asynchronous client library. It is not a UI
framework and it does not contain AI/LLM character logic. The D0 milestone is
documentation and contract work only; no network client or public C++ API has
been implemented yet.

## Documentation

- [Architecture](docs/ARCHITECTURE.md) and [invariants](docs/INVARIANTS.md)
- [Glossary](docs/GLOSSARY.md)
- [Roadmap](docs/ROADMAP.md) and [implementation status](docs/IMPLEMENTATION_STATUS.md)
- [API contracts](docs/contracts/CLIENT_API.md)
- [Engineering guides](docs/guides/coding-style.md)
- [ADR-0001](docs/adr/0001-client-architecture.md)
- [ADR-0002](docs/adr/0002-async-execution-contract.md)
- [Reference scenarios](docs/reference-scenarios.md)

The canonical documentation language is English. All examples in D0 are
conceptual and do not imply an implemented API.

## Scope and status

D0 establishes boundaries, typed asynchronous contracts, identity and
authorization rules, delivery/lifetime semantics, persistence seams, and a
dependency-gated implementation plan. M0c fixes the async execution contract
before protocol implementation. See
[`IMPLEMENTATION_STATUS.md`](docs/IMPLEMENTATION_STATUS.md) for the evidence
behind every status.

## License

See [LICENSE](LICENSE).
