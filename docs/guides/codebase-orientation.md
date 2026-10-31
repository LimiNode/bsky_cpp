# Codebase orientation

`bsky_cpp` keeps public headers and implementations together under
`src/bsky_cpp/`. Each directory represents an ownership boundary:

- `core/` contains reusable foundation primitives: `Error`, `Result`,
  cancellation, and version reporting.
- `atproto/` contains protocol value types and validation for DID, Handle, NSID,
  AT URI, CID, and StrongRef.
- `transport/` contains transport-neutral request/response values and the
  synchronous adapter seam. It does not select an HTTP implementation.

Future domains are added only when they have a real public boundary. Do not add
empty directories or placeholder classes. Private implementation headers stay
in a domain-local `detail/` directory and are never installed.

The complete public API is available through:

```cpp
#include <bsky_cpp.hpp>
```

Focused code may include a narrow domain header such as
`<bsky_cpp/atproto/identifiers.hpp>`. The root CMake variable
`BSKY_PUBLIC_HEADERS` is the single allowlist used to install headers, generate
Doxygen input, and build header self-containment checks. Preserve its relative
layout under `include/` and do not recursively install `src/bsky_cpp/`.

When adding a public type, update the allowlist, umbrella header when
appropriate, Doxygen comments, tests, installation/export checks, and the
implementation status documentation in one change.
