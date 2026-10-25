# Implementation status

This inventory is factual for the repository state represented by this branch.
A document, example, or roadmap item is not evidence of an implementation.

## Implemented

- Repository metadata: `README.md`, `LICENSE`, and `.gitignore`.
- D0 architecture and engineering documentation in this branch.
- M0a scaffold: C++17 CMake target, public include layout, version contract,
  smoke test, CTest integration, install/export package configuration,
  Markdown link checker, and Windows/Linux/macOS CI definitions.

## Partial

- M0 is partial: M0a and the M0b `Result`, typed `Error`, and basic
  cancellation source/token primitives are implemented; async executor,
  callback, and network machinery remain deferred.

## Contract-only

- High-level client and low-level XRPC API contracts.
- Full async vocabulary, callback registration/quiescence machinery, and
  network operation futures remain contract-only while ADR-0001 is open.
- Identity/discovery, authentication, posts/threads/media, social/profile/chat,
  events/streaming, delivery/lifetime, and persistence seams.
- Coding, dependency, agent, documentation, and commit governance.

## Planned

- Remaining M0 async integration through M5 work described in
  [`ROADMAP.md`](ROADMAP.md).

## Deferred

- Network integrations, generated Lexicon classes, event-hub-cpp/MDBX adapters,
  full async callback/executor machinery, and AI/LLM logic.

## Evidence rule

Each future status change must name a source file, test, or reproducible command
as evidence and must update the affected contract. “Conceptual” snippets remain
conceptual until compiled and tested.
