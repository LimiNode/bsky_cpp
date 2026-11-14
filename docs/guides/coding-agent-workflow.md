# Coding-agent workflow

1. Read `AGENTS.md`, the architecture, invariants, roadmap/status, and affected
   contract.
2. Inspect the current branch and worktree; preserve unrelated user changes.
3. State scope, assumptions, and open questions in the change or ADR.
4. Make the smallest coherent change. For D0, that means documentation only.
5. Update status and links together with contract changes.
6. Run `git diff --check`; run build/tests when executable code exists; run
   relative-link and terminology checks for documentation.
7. Review the diff for secrets, accidental generated files, production code,
   dependency changes, and claims unsupported by evidence.
8. Report exact HEAD SHA, files, checks, unresolved questions, and the next
   dependency-gated milestone. Draft PRs remain draft until independent review.

Repository layout policy: source and public headers live together under
`src/bsky_cpp/`; generated build, install, and external-consumer trees belong
under `build/`; approved managed dependencies use flat `external/<dependency>/`
submodules.

Public API layout policy:

- `core/` owns foundational result, error, cancellation, and version types;
  `atproto/` owns protocol identifiers; `transport/` owns transport-neutral
  request/response values and seams; `runtime/` is the reviewed opt-in
  operation-lifetime domain and is the only current domain that requires
  `event-hub-cpp`.
- The umbrella include is `<bsky_cpp.hpp>`; narrow includes should use the
  owning domain path. It covers the foundation surface; runtime consumers use
  `<bsky_cpp/runtime/runtime.hpp>` and
  `<bsky_cpp/runtime/async_transport.hpp>` explicitly. The root CMake
  `BSKY_PUBLIC_HEADERS` list is canonical for installation, Doxygen input, and
  compile-only header checks.
- Keep private implementation headers under a domain-local `detail/` and do
  not create empty future-domain directories.
- For documentation changes, run `cmake -S . -B build/docs
  -DBSKY_BUILD_DOCUMENTATION=ON` followed by `cmake --build build/docs --target
  bsky_cpp_docs`. Doxygen warnings are errors in CI.
