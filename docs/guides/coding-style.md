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
- Use four spaces. Keep comments factual and short; do not use ImGui-style
  naming.
- Document public declarations with English Doxygen: purpose, parameters,
  result, typed errors, ownership/lifetime, thread safety, cancellation, side
  effects, and network guarantees.
