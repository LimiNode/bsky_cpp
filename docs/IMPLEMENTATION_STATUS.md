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

- M1c's merged runtime subset (PR #15, merge commit
  `85d456b0e26f9c3b3a4822d6e0da8ba005e212ac`) is implemented
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
  remaining gates. This subset has passed independent source review and the
  post-merge workflow [42820527726](https://github.com/LimiNode/bsky_cpp/actions/runs/42820527726)
  (17/17 jobs). Full M1c remains partial: the optional future adapter and the
  native asynchronous transport/client acceptance gates are intentionally
  deferred to M1d.

### M1c conformance audit (2026-11-10)

The following matrix records the post-merge audit against ADR-0002,
ADR-0004--ADR-0007, and `contracts/OPERATION_RUNTIME.md`.

| Invariant | Classification | Evidence |
| --- | --- | --- |
| Managed/external composition and lifecycle handoff | Implemented and test-confirmed | `tests/runtime.cpp` lifecycle, external-consumer, deadline, and self-destruction cases; runtime CI matrix |
| Admission validation, bounded capacity, and backpressure | Implemented and test-confirmed | `admission_capacity_and_terminal_races()`, blocking bridge capacity cases |
| Exactly one terminal outcome and first-winner cancellation/completion | Implemented and test-confirmed | cancellation, typed terminal-race, deadline, and blocking bridge race cases |
| Callback registration closure, suppression, capture release, and quiescence | Implemented and test-confirmed | `close_and_move_only_captures()`, executor lifecycle, typed callback lifetime, shutdown cases |
| Move-only typed results and callbacks | Implemented and test-confirmed | `typed_operations()`, `typed_callback_lifetime()`, compile-time payload checks |
| Executor ownership, rejection, and accepted-but-dropped tasks | Implemented and test-confirmed | `injected_executor_lifecycle()` and callback rejection tests |
| Shutdown timeout, repeated shutdown, exception containment, and lifetime leases | Implemented and test-confirmed | shutdown/lifetime/fault tests plus TSan and sanitizer jobs |
| Cooperative `steady_clock` deadlines | Implemented and test-confirmed | `operation_deadline_semantics()` in managed and external modes |
| Bounded synchronous transport bridge and private-worker wake-up ordering | Implemented and test-confirmed | `blocking_transport_bridge_semantics()`; PR #15 gate-controlled regressions |
| Foundation/package isolation and installed runtime consumers | Implemented and test-confirmed | runtime-off/on CMake jobs, installed consumers, header/Doxygen checks |
| Optional `std::future` adapter | Consciously deferred/optional | ADR-0002 and `OPERATION_RUNTIME.md`; no future adapter is required by M1c |
| Native async transport, kurlyk/XRPC mapping, and high-level Client | Consciously deferred/optional | ROADMAP M1d; outside the merged runtime subset |

No M1c invariant was found that is implemented but lacks a required
regression, and no unimplemented item is a blocker for the documented merged
subset. The broader milestone remains partial until the explicitly deferred
M1d boundaries are delivered and reviewed.

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
