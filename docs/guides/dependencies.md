# Dependency policy

The foundation/value targets have no mandatory HTTP, event, or persistence
dependency. The future production async `Client`/runtime component is
event-hub-cpp-backed and therefore has a mandatory runtime dependency when that
component is selected. `kurlyk` is the preferred, replaceable network adapter;
MDBX is an optional persistence implementation; application-level event
subscriptions and stream sources remain optional. None of these types leak
through foundation DTOs.

Adding a dependency requires an ADR or an update to the relevant ADR, a reason
it cannot remain optional, license/security review, a C++17 compatibility check,
and a removal or replacement plan. Generated Lexicons are build inputs, not a
reason to leak generator/runtime types into DTOs.

When a managed dependency is approved, add it as a flat submodule under
`external/<dependency>/` and pin the gitlink to a reviewed commit SHA. Do not
initialize submodules recursively by default. Nested or transitive submodules
are allowed only with a documented technical exception; compatible shared
dependencies should be reused rather than duplicated. The foundation must
configure, build, and test without initialized optional dependencies. Runtime
and adapter components are opt-in build/package components with explicit
dependency discovery and export requirements.

Stage 2 records the intended target topology; it is not implemented yet:

| Component | Target shape | Dependency | Installation consequence |
| --- | --- | --- | --- |
| Foundation | `bsky_cpp::bsky_cpp` (current target) | none beyond the C++ standard library | installed by default; existing consumers remain dependency-free |
| Runtime | future `bsky_cpp::runtime` | `event-hub-cpp::event-hub-cpp` and Threads | opt-in component; config package uses `find_dependency(event-hub-cpp CONFIG)` |
| Client/transport adapter | future `bsky_cpp::kurlyk_adapter` | `bsky_cpp::runtime` plus `kurlyk::kurlyk` package and its exported transitive dependencies | opt-in adapter; never part of foundation DTOs |
| Streams | future optional stream component | selected HTTP/WebSocket adapter | installed only when selected; replay/backpressure contract remains bsky-owned |
| Persistence | future optional component | MDBX adapter | installed only when selected |

The implementation PR must prefer installed package targets over nested
`add_subdirectory()` calls. If source composition is unavoidable, it must add
each pinned dependency once, never add a second physical Time Shield copy, set
Time Shield NTP explicitly `OFF`, and isolate cache variables in a documented
toolchain/preset. Installed config files must use `find_dependency()` for every
transitive target they expose.
