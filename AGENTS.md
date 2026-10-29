# bsky_cpp agent and contributor guide

## Mission

`bsky_cpp` is a general-purpose Modern C++17 client SDK for Bluesky and the AT
Protocol. Keep protocol mechanics and dependable client primitives in the SDK;
keep UI, orchestration, prompts, LLM inference, persona memory, and autonomous
decision-making in applications.

## Source of truth

The documentation in `docs/` is normative until an explicitly reviewed contract
change is made. Executable code and tests implement the contract; they do not
silently supersede it. Read `ARCHITECTURE.md`, `INVARIANTS.md`, `ROADMAP.md`,
`IMPLEMENTATION_STATUS.md`, and the relevant contract before changing a public
surface. Open questions must remain explicitly marked as open questions.

## Non-negotiable boundaries

- The core is C++17 and transport-neutral. `kurlyk` is the recommended adapter,
  not a type leaked through high-level DTOs.
- `event-hub-cpp` and MDBX are optional integrations. The core must build and
  operate without either one.
- Never claim an operation is implemented because a conceptual snippet or a
  document exists. Update implementation status with evidence.
- If implementation behavior must differ, update the contract and ADR/status in
  the same change; record compatibility and migration implications.
- Never store bearer tokens or client secrets in logs or an unencrypted
  backend without an explicit security policy.
- No hidden blocking calls in callbacks or event handlers.
- Preserve UTF-8 byte offsets, Unicode code-point counts, and grapheme counts as
  distinct concepts.

## Repository layout and dependencies

- Colocate library `.hpp` and `.cpp` files under `src/bsky_cpp/`.
- Keep generated build, install, and external-consumer trees under `build/`.
- Put approved managed dependencies in flat, direct Git submodules at
  `external/<dependency>/`; pin each gitlink to a reviewed commit SHA.
- Do not initialize submodules recursively by default. Nested/transitive
  submodules require a documented technical exception, and compatible shared
  dependencies should be reused rather than duplicated.
- The core must configure, build, and test without initialized optional
  dependencies.
- Keep the core target independent of optional transport, event, and persistence
  integrations.

## Documentation and review workflow

1. State the affected contract and lifecycle/concurrency implications.
2. Keep public names C++17-compatible: `CamelCase` types, `snake_case` methods,
   `m_` data members, `CamelCase` enum values, lowercase namespaces, and `BSKY_`
   macros only when unavoidable.
3. Document public API intent, parameters, result, typed errors, ownership,
   lifetime, thread safety, cancellation, and network guarantees in Doxygen.
4. Add or update an ADR when a decision changes a boundary or dependency.
5. Run `git diff --check` and link-check or grep checks before review.

See `docs/guides/coding-agent-workflow.md` for the complete Definition of Done.

Canonical local build commands use generated trees under `build/`:

```text
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DBSKY_BUILD_TESTS=ON
cmake --build build/debug --parallel
ctest --test-dir build/debug --output-on-failure
```

Use `build/release`, `build/consumer`, and `build/sanitized` for the analogous
Release, installed-consumer, and sanitizer outputs.
