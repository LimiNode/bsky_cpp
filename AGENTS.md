# bsky_cpp agent and contributor guide

## Mission

`bsky_cpp` is a general-purpose Modern C++17 client SDK for Bluesky and the AT
Protocol. Keep protocol mechanics and dependable client primitives in the SDK;
keep UI, orchestration, prompts, LLM inference, persona memory, and autonomous
decision-making in applications.

## Source of truth

The documentation in `docs/` is normative until executable code and tests
supersede a contract. Read `ARCHITECTURE.md`, `INVARIANTS.md`, `ROADMAP.md`,
`IMPLEMENTATION_STATUS.md`, and the relevant contract before changing a public
surface. Open questions must remain explicitly marked as open questions.

## Non-negotiable boundaries

- The core is C++17 and transport-neutral. `kurlyk` is the recommended adapter,
  not a type leaked through high-level DTOs.
- `event-hub-cpp` and MDBX are optional integrations. The core must build and
  operate without either one.
- Never claim an operation is implemented because a conceptual snippet or a
  document exists. Update implementation status with evidence.
- Never store bearer tokens or client secrets in logs or an unencrypted
  backend without an explicit security policy.
- No hidden blocking calls in callbacks or event handlers.
- Preserve UTF-8 byte offsets, Unicode code-point counts, and grapheme counts as
  distinct concepts.

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
