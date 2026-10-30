# ADR-0003: Domain-oriented source and public-header layout

## Status

Accepted for Stage 1 after M1b.

## Context

The initial M0/M1 implementation colocated every public header directly under
`src/bsky_cpp/`. That layout was adequate for the foundation, but it made
ownership boundaries unclear as runtime, XRPC, identity, authentication, data,
and streaming domains are added.

## Decision

Keep `.hpp`/`.cpp` files colocated while grouping current public code by domain:

- `core/` owns `Error`, `Result`, cancellation, and version primitives;
- `atproto/` owns protocol identifier values;
- `transport/` owns the transport-neutral HTTP seam and fake transport.

`src/bsky_cpp.hpp` is the single umbrella include for the currently implemented
public API. Future domains are documented boundaries only; empty directories and
placeholder classes are not added ahead of implementation.

The public include paths change to the domain paths listed in
[`ARCHITECTURE.md`](../ARCHITECTURE.md). This pre-1.0 SDK does not install
forwarding headers for the old flat paths. C++ type names, namespaces, behavior,
and ABI-relevant implementation contracts are unchanged.

The CMake install surface remains an explicit allowlist. It installs the
umbrella header at `include/bsky_cpp.hpp` and domain headers below
`include/bsky_cpp/{core,atproto,transport}`. No private `detail/` headers are
installed.

## Consequences

The source tree communicates ownership and keeps future additions localized.
Consumers can include one stable umbrella or a narrow domain header. Existing
pre-1.0 callers must update include paths; no runtime migration is required.

An optional Doxygen target documents exactly the public allowlist and emits only
under `build/docs/doxygen/`; normal library builds do not require Doxygen.

## Rejected alternatives

- Keeping a flat directory: does not scale as domains grow.
- Renaming C++ types/namespaces to match directories: unnecessary API churn.
- Installing every header recursively: would expose private implementation files.
- Creating empty future directories/classes: adds no usable contract.
