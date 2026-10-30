# Roadmap

Progress is dependency-gated. A later milestone cannot silently bypass an
invariant or contract from an earlier one.

## D0 — Architecture Foundation (complete)

- **Scope:** documentation, contracts, governance, terminology, and ADR.
- **Prerequisites:** repository baseline and current AT Protocol references.
- **Deliverables:** architecture, invariants, all contract documents, guides,
  status, scenarios, and roadmap.
- **Acceptance:** links resolve, terms are consistent, every contract states
  inputs/outputs/errors/lifetime/concurrency, and no implementation is implied.
- **Testing:** `git diff --check`, documentation link and terminology checks.
- **Deferred:** all production code, dependencies, generated schemas, and
  network integrations.

## M0 — Engineering Scaffold

- **Prerequisites:** D0 contracts approved.
- **Deliverables:** CMake targets, C++17 library layout, test harness, CI,
  `Result`/error/cancellation primitives, and documentation checks.
- **Acceptance:** clean configure/build/test with no optional backend required;
  negative and cancellation paths are tested.
- **Testing:** compiler matrix, sanitizers, unit tests, and diff/link checks.
- **Deferred:** real XRPC calls and generated Lexicons.

### M0a — Project scaffold (complete)

- **Scope:** C++17 CMake library target, public include layout, install/export,
  smoke test, CTest, documentation link check, and cross-platform CI.
- **Acceptance:** a consumer uses `find_package(bsky_cpp CONFIG REQUIRED)` and
  links `bsky_cpp::bsky_cpp` without optional dependencies.
- **Deferred:** Result/Error/cancellation semantics and all network behavior.

### M0b — Core primitives (complete)

- **Prerequisites:** M0a.
- **Deliverables:** `Result<T>`, `Result<void>`, typed `Error`, diagnostic
  metadata redaction, cancellation source/token, and focused tests.
- **Acceptance:** C++17 success/error paths, move-only results, concurrent and
  repeated cancellation, owner destruction, and applicable race tests pass.
- **Deferred:** network futures, executors, and callback machinery until the
  async vocabulary is selected by ADR.

### M0c — Async API and execution contract (docs-only)

- **Prerequisites:** M0b primitives and ADR-0001.
- **Deliverables:** ADR-0002, callback-first operation contracts,
  `OperationHandle`/executor/lifetime policy, conceptual C++17 snippets, and
  the async conformance test plan.
- **Acceptance:** admission, terminal outcome, callback delivery, cancellation
  races, executor ownership, shutdown/quiescence, backpressure, and optional
  future-adapter boundaries are explicit and internally consistent.
- **Testing:** Markdown link checks, terminology review, and conformance cases
  ready for deterministic fake transport/executor implementation.
- **Deferred:** transport, XRPC, executor implementation, and callback
  machinery remain out of scope.

### M1a — Identifier primitives (implemented)

- **Prerequisites:** M0a/M0b primitives and the M0c contract.
- **Deliverables:** strict `Did`, `Handle`, `Nsid`, `RecordKey`, `AtUri`, `Cid`,
  and `StrongRef` value types with structural validation, canonical forms, and
  public consumer coverage.
- **Acceptance:** malformed identifiers are rejected with typed errors; CID
  validation decodes multibase/varint/multihash structure; AT Protocol CID and
  StrongRef restrictions are explicit in
  [`IDENTIFIERS.md`](contracts/IDENTIFIERS.md).
- **Testing:** pinned upstream syntax fixtures cover 335 valid/invalid DID,
  Handle, NSID, record-key, and AT URI cases; focused CID/StrongRef tests,
  install/export consumer use, Debug/Release builds, and sanitizer builds.
- **Deferred:** DID/handle resolution, transport, XRPC, persistence, and
  authenticated operations.

### M1b — Transport abstraction (implemented)

- **Prerequisites:** M1a and the M1 transport-neutral boundary.
- **Deliverables:** owned typed transport request/response values, a synchronous
  transport interface, header/body validation, cancellation observation, and a
  deterministic fake transport.
- **Acceptance:** no concrete HTTP dependency in the core; malformed URLs and
  headers are rejected; request/response bodies own their bytes; cancelled
  requests do not dispatch; installed consumers retain the public API.
- **Testing:** Debug/Release CTest, cancellation and header validation tests,
  install/export consumer, and sanitizer CI.
- **Deferred:** async operation admission/execution (M1c), XRPC encoding and
  protocol mapping (M1d), identity resolution, and network adapters.

## M1 — AT Protocol Foundation

- **Prerequisites:** M0c; current XRPC/Lexicon specifications pinned for
  tests.
- **Deliverables:** transport adapter, XRPC query/procedure API, DID/Handle
  resolution, AT URI/CID/StrongRef values, discovery and public read APIs.
- **Acceptance:** typed protocol errors, safe endpoint selection, timeout and
  cancellation behavior, and conformance fixtures.
- **Testing:** wire fixtures, malformed input, redirect/SSRF policy, and retry
  safety tests.
- **Deferred:** authenticated writes, UI, and AI orchestration.

## M2 — Authenticated Client

- **Prerequisites:** M1 and a reviewed credential-store seam.
- **Deliverables:** App Password sessions, posts/replies/quotes, thread
  builder, media upload, rich text, likes/follows/blocks, and profiles.
- **Acceptance:** StrongRef-correct replies, partial thread results, secret
  redaction, MIME/size validation, explicit operation ownership, and
  process-lifetime reconciliation for uncertain/partial publication.
- **Testing:** Lexicon fixtures, operation-specific idempotency/retry tests,
  Unicode facet tests, and in-process recovery tests. D0/M2 do not promise
  restart-safe recovery without durable journal/outbox support.
- **Deferred:** OAuth, durable stream processing, and DM.

## M3 — Social Client

- **Prerequisites:** M2 and current app/chat schemas.
- **Deliverables:** notifications, direct messages, multiple accounts,
  advanced media, and remaining social operations.
- **Acceptance:** public notifications/replies are distinct from private chat;
  routing and authorization are explicit.
- **Testing:** account isolation, pagination, permission failures, and load
  limits.
- **Deferred:** production-grade replay and optional integrations.

## M4 — Advanced Infrastructure

- **Prerequisites:** M1–M3 contracts and security review.
- **Deliverables:** AT Protocol OAuth (PAR, PKCE, DPoP, scopes, refresh),
  Jetstream/stream source, optional persistence, durable operation journal and
  outbox recovery, and event-hub integration.
- **Acceptance:** identity verification, replay/checkpoint semantics,
  cursor-expiry/gap outcomes, backpressure, restart-safe reconciliation when a
  durable backend is configured, graceful shutdown, and no mandatory optional
  dependency.
- **Testing:** fault injection, duplicate/reorder/reconnect scenarios, and
  token-rotation tests.
- **Deferred:** exactly-once claims and provider-specific guarantees.

## M5 — Production Hardening

- **Prerequisites:** all required feature milestones and operational owners.
- **Deliverables:** conformance suite, fault injection, recovery drills,
  concurrency/security audits, observability, and release criteria.
- **Acceptance:** documented SLOs, reproducible builds, threat-model closure,
  and reviewed migration/recovery procedures.
- **Testing:** continuous integration matrix, fuzzing, sanitizers, and soak tests.
- **Deferred:** features without a supported protocol contract.
