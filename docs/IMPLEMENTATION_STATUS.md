# Implementation status

This inventory is factual for the repository state represented by this branch.
A document, example, or roadmap item is not evidence of an implementation.

## Implemented

- Repository metadata: `README.md`, `LICENSE`, and `.gitignore`.
- D0 architecture and engineering documentation (merged on `main`).
- M0a scaffold: C++17 CMake target, public include layout, version contract,
  smoke test, CTest integration, install/export package configuration,
  Markdown link checker, and Windows/Linux/macOS CI definitions.
- M0b core primitives: `Result<T>`, `Result<void>`, typed `Error`, bounded
  diagnostic metadata, and cooperative cancellation with race-focused tests.
- M1a identifier primitives: strict `Did`, `Handle`, `Nsid`, `RecordKey`,
  `AtUri`, structural `Cid`, and `StrongRef` parsing in
  [`identifiers.hpp`](../src/bsky_cpp/identifiers.hpp), with unit and
  installed-consumer coverage.

## Partial

- M0 is complete as scoped: M0a/M0b are implemented and M0c is an accepted
  documentation-only async contract. Async executor, callback, and network
  machinery remain deferred.

## Contract-only

- High-level client and low-level XRPC API contracts.
- The M0c async vocabulary, callback registration/quiescence contract, and
  optional future-adapter boundary are documented in ADR-0002 and remain
  contract-only; no executor or callback implementation is included.
- Identity/discovery, authentication, posts/threads/media, social/profile/chat,
  events/streaming, delivery/lifetime, and persistence seams.
- DID/handle resolution and endpoint discovery remain contract-only; M1a only
  validates identifier values.
- Coding, dependency, agent, documentation, and commit governance.

## Planned

- M1b and later protocol/transport work through M5 as described in
  [`ROADMAP.md`](ROADMAP.md).

## Deferred

- Network integrations, generated Lexicon classes, event-hub-cpp/MDBX adapters,
  full async callback/executor machinery, and AI/LLM logic.

## Evidence rule

Each future status change must name a source file, test, or reproducible command
as evidence and must update the affected contract. “Conceptual” snippets remain
conceptual until compiled and tested.
