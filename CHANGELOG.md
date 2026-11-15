# Changelog

All notable changes to `bsky_cpp` are recorded here. The project is pre-1.0;
minor releases may change public APIs and remain on their own compatibility
line.

## [Unreleased]

- Continue M1d protocol and concrete transport work after the preview gate.

## [0.1.0-alpha.1] - 2026-11-15

This is an engineering preview of the C++17 foundation and opt-in operation
runtime. It is suitable for build, packaging, and lifecycle integration tests;
it is not a complete Bluesky client.

### Included

- Foundation `Result`, typed `Error`, cancellation, version, AT Protocol
  identifier, and transport-neutral request/response primitives.
- Optional event-hub-backed operation runtime with callback-first typed results,
  deadlines, bounded admission, cooperative cancellation, and shutdown
  quiescence semantics.
- Transport-neutral native async completion seam and deterministic fake
  transport.
- CMake install/export consumers, public-header checks, Doxygen, and the
  Windows/Linux/macOS CI matrix with Linux sanitizer and TSan jobs.

### Not included

- Bluesky network `Client`, XRPC codecs, authentication, DID resolution,
  `kurlyk` adapter, retries, persistence, streams, and social operations.
