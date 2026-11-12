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
  [`atproto/identifiers.hpp`](../src/bsky_cpp/atproto/identifiers.hpp), with unit and
  installed-consumer coverage.
- M1b transport seam: owned typed request/response values, the transport
  interface, and deterministic cancellation-aware `FakeTransport` in
  [`transport/transport.hpp`](../src/bsky_cpp/transport/transport.hpp), with unit coverage.
- Stage 1 source layout: domain-oriented `core/`, `atproto/`, and `transport/`
  headers with the canonical [`bsky_cpp.hpp`](../src/bsky_cpp.hpp) umbrella
  include and an optional Doxygen documentation target. The public-header
  allowlist generates installation, Doxygen input, and compile-only API checks;
  CI runs the documentation target with warnings treated as errors.

## Partial

- Draft M1c non-blocking void and typed-result operation runtime is implemented
  and tested in
  [`runtime.cpp`](../src/bsky_cpp/runtime/runtime.cpp) and
  [`tests/runtime.cpp`](../tests/runtime.cpp). Admission/stop serialization,
  startup/finalization lifecycle serialization,
  startup/consumer handoff serialization,
  first-winner outcomes, typed move-only payload delivery, callback capture
  quiescence, bounded capacity, self-destruction, and component-isolated
  exports are covered. Typed delivery requires a nothrow-move-constructible
  `Result<T>`; throwing payload moves are rejected at compile time. The public
  `IExecutor` callback-dispatch seam accepts move-only tasks, reports rejection,
  and observes accepted-but-dropped tasks so callback quiescence remains
  finite; the default adapter continues to use the existing `ModuleHub`.
  Optional cooperative operation deadlines are implemented with one delayed
  `TaskManager` task per admitted deadline, including first-winner timeout
  semantics, managed/external dispatch, cancellation cleanup, typed timeout
  delivery, and callback suppression tests. The opt-in blocking transport bridge
  is implemented through `Runtime::submit_blocking()` with one bounded
  ModuleHub-owned private worker, shared typed terminalization, cooperative
  cancellation/deadlines, and explicit transport lifetime leases. See the
  [runtime contract](contracts/OPERATION_RUNTIME.md) and
  [deadline ADR](adr/0006-operation-deadlines.md) for exact guarantees and
  remaining gates. Full M1c is not yet complete or independently approved.

- M1d1 native transport slice: `Runtime::submit_async()` forwards one owned
  response completion through the existing operation state machine without
  `future.get()`, per-request waiters, or a second scheduler. The public
  `AsyncTransport` boundary and deterministic `FakeAsyncTransport` live in
  [`runtime/async_transport.hpp`](../src/bsky_cpp/runtime/async_transport.hpp);
  [`tests/runtime.cpp`](../tests/runtime.cpp) covers synchronous adapter
  rejection, inline and delayed completion, duplicate/late suppression, and
  cancellation. This does not implement kurlyk, XRPC mapping, retries, or a
  high-level Client.

- M0 is complete as scoped: M0a/M0b are implemented and M0c remains the
  accepted documentation contract. Managed timed awaiter guarantees in the
  partial M1c runtime are covered by the pinned event-hub-cpp PR #13 merge.
- Stage 2 dependency reuse audit: the pinned `event-hub-cpp` snapshot is
  integrated only by the opt-in M1c runtime target; the foundation target still
  has no runtime dependency. The current gitlink is the upstream PR #13 merge
  commit; ownership and compatibility decisions remain recorded in
  [`DEPENDENCY_REUSE_AUDIT.md`](research/DEPENDENCY_REUSE_AUDIT.md) and
  ADR-0004.

## Contract-only

- High-level client and low-level XRPC API contracts.
- The optional future adapter remains contract-only. Typed result operations are implemented only
  through the callback-first `submit_typed<T>()` subset; this does not imply a
  future adapter or a payload-bearing handle.
- Identity/discovery, authentication, posts/threads/media, social/profile/chat,
  events/streaming, delivery/lifetime, and persistence seams.
- DID/handle resolution and endpoint discovery remain contract-only; M1a only
  validates identifier values.
- Coding, dependency, agent, documentation, and commit governance.

## Planned

- M1d and later protocol/transport work through M5 as described in
  [`ROADMAP.md`](ROADMAP.md).

## Deferred

- Network integrations, generated Lexicon classes, kurlyk/MDBX adapters,
  XRPC mapping, and AI/LLM logic.

## Evidence rule

Each future status change must name a source file, test, or reproducible command
as evidence and must update the affected contract. “Conceptual” snippets remain
conceptual until compiled and tested.
