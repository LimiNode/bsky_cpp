# Dependency policy

The core has no mandatory HTTP, event, or persistence dependency. `kurlyk` is
the recommended transport implementation; `event-hub-cpp` is an optional event
integration; MDBX is an optional persistence implementation. Each is injected
behind a stable interface and tested independently.

Adding a dependency requires an ADR or an update to the relevant ADR, a reason
it cannot remain optional, license/security review, a C++17 compatibility check,
and a removal or replacement plan. Generated Lexicons are build inputs, not a
reason to leak generator/runtime types into DTOs.

When a managed dependency is approved, add it as a flat submodule under
`external/<dependency>/` and pin the gitlink to a reviewed commit SHA. Do not
initialize submodules recursively by default. Nested or transitive submodules
are allowed only with a documented technical exception; compatible shared
dependencies should be reused rather than duplicated. The core must configure,
build, and test without initialized optional dependencies.
