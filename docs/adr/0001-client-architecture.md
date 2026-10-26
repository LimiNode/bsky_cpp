# ADR-0001: Client architecture and optional infrastructure

- **Status:** Accepted for D0; async execution details are refined by
  [ADR-0002](0002-async-execution-contract.md).
- **Date:** 2026-10-25

## Context

The project needs a reusable Bluesky/AT Protocol SDK for synchronous-looking
application code, autonomous applications, and future UI clients. Protocol
operations, identity, network failure, and durable recovery have different
lifecycle and security needs. Existing supporting projects provide useful
implementations but must not dictate high-level DTOs.

## Decision

Build a C++17, async-first client with separate high-level domain and low-level
XRPC APIs. Inject transport, execution, credentials, clocks, and persistence.
Use kurlyk as the recommended transport adapter; keep event-hub-cpp and MDBX
optional. Keep AI/LLM logic outside the SDK. Treat network calls as fallible
operations and durable application operations as explicit state machines rather
than pretending multi-step publication is atomic.

## Rationale

Async-first prevents hidden blocking in event-driven applications and makes
cancellation, rate limits, backpressure, and bounded concurrency explicit. A
high-level API is ergonomic for common operations while low-level XRPC preserves
coverage for new Lexicons. Optional integrations keep the core portable and
testable. A transport adapter isolates wire concerns and prevents dependency
types from leaking into stable DTOs. Excluding AI logic keeps the SDK reusable,
auditable, and protocol-focused.

## Consequences

Implementations need explicit executors, operation-specific retry policies, and
typed errors. Applications must handle partial progress, duplicate stream
delivery, token refresh, and `OutcomeUnknown` reconciliation; no generic
idempotency key or restart-safe recovery is implied before durable M4 support.
More interfaces are required up front, but tests can use in-memory and fake
adapters without a network or MDBX installation.

## Open questions

- Which concrete executor adapters should be supported first under
  [ADR-0002](0002-async-execution-contract.md)?
- Which supported stream providers and event ordering guarantees should M4
  standardize?
- What protected credential stores should official adapters target on each OS?
- Which Lexicon generation strategy best preserves unknown-field behavior?
