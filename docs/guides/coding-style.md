# C++ coding style

- Target C++17; do not use C++20-only public APIs.
- Use `CamelCase` for classes, structs, and types; `snake_case` for functions
  and methods; `m_` for data members; `CamelCase` enum values; lowercase
  namespaces; and `BSKY_` macros only when needed.
- Prefer RAII, const-correctness, value semantics, explicit ownership, and
  narrow interfaces. Avoid unnecessary copies of large DTOs and unexplained
  default lambda captures.
- Keep transport and persistence types behind interfaces. Public headers must
  compile without optional integrations.
- Organize library sources under the owning domain in `src/bsky_cpp/`:
  `core/`, `atproto/`, or `transport/`. Colocate public `.hpp` and
  implementation `.cpp` files. Keep `tests/` fixtures and `examples/` opt-in
  examples separate. Future domains need a reviewed boundary; do not create
  empty placeholder directories.
- Keep private headers in a domain-local `detail/` directory. Do not put
  unrelated types into generic `utils/`, `data/`, or `enums/` directories.
  Keep one primary type per file where practical.
- Use `.hpp` for public C++ declarations, `.cpp` for compiled implementations,
  and `.ipp`/`.tpp` only for template implementation fragments included by the
  owning header. Do not introduce a second extension convention without an
  ADR.
- Use `#pragma once`; include the matching public header first in a `.cpp`, then
  project headers, standard headers, and third-party headers in separate
  groups. Do not rely on transitive includes or import private headers from a
  public header.
- Use four spaces. Keep comments factual and short; do not use ImGui-style
  naming. Avoid magic domain values and default lambda captures (`[&]`, `[=]`).
- Prefer `///` Doxygen in English. Give every public header a `\file` and
  `\brief` block. Use `\brief`, `\tparam`, `\param`,
  `\return`, `\throws`, `\pre`, `\post`, `\invariant`, `\complexity`,
  a `Thread safety` paragraph, and `\note`/`\warning` when meaningful. Every public
  parameter and non-void return is documented.
- Public documentation states ownership/lifetime, thread safety, cancellation,
  callback quiescence, side effects, and network guarantees; `Thread-safe`,
  `Not thread-safe`, or a precise conditional statement is required.
