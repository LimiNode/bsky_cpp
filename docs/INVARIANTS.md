# Architectural invariants

These rules are testable acceptance criteria for every implementation milestone.

1. **C++ baseline:** public core APIs compile as C++17; no C++20-only feature is
   required.
2. **Layering:** high-level DTOs do not expose kurlyk, event-hub-cpp, MDBX, or
   another concrete backend type.
3. **Completion:** each asynchronous operation completes at most once, and no
   user callback is called after its registered lifetime or cancellation scope.
4. **Cancellation:** cancellation is cooperative and observable; shutdown
   drains or explicitly aborts in-flight work according to the operation's
   contract.
5. **Retry safety:** automatic retry is limited to idempotent operations or an
   explicitly supplied idempotency key. Record creation is never duplicated by
   an invisible retry.
6. **Identity:** a DID is stable account identity; a Handle is mutable naming.
   A Handle must not be used as a durable primary key.
7. **References:** reply roots and parents are StrongRefs (CID plus AT URI), not
   display text or an unverified URL.
8. **Text offsets:** UTF-8 byte offsets, Unicode code points, and grapheme
   clusters are separate units and are never silently substituted.
9. **Secrets:** access/refresh tokens and app passwords are excluded from logs,
   diagnostics, crash reports, and plaintext persistence by default.
10. **Protocol truth:** limits and field meanings come from current Lexicon and
    AT Protocol specifications; documentation labels assumptions and open
    questions.
11. **Partial progress:** multi-step publication reports successful and
    unresolved steps separately; it never claims atomicity across records.
12. **Streams:** delivery is at-least-once unless a source explicitly says
    otherwise. Consumers must tolerate duplicates and use cursors/checkpoints.
13. **Persistence optionality:** an in-memory implementation is a valid core
    configuration; durable backends are replaceable.
14. **Unknown data:** unknown fields and event variants do not cause unsafe
    memory access or credential disclosure; callers can inspect an explicit
    unknown representation where required.
