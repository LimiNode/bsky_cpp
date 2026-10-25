# Architectural invariants

These rules are testable acceptance criteria for every implementation milestone.

1. **C++ baseline:** public core APIs compile as C++17; no C++20-only feature is
   required.
2. **Layering:** high-level DTOs do not expose kurlyk, event-hub-cpp, MDBX, or
   another concrete backend type.
3. **Completion:** an accepted asynchronous operation reaches exactly one
   internal terminal outcome. A cancellation request is not itself completion:
   it only asks the operation to stop work that has not started or cannot safely
   continue. Each callback registration receives at most one delivery, and
   closing that registration may suppress its delivery without changing the
   operation's terminal outcome.
4. **Cancellation and callbacks:** cancellation is cooperative and observable.
   If cancellation wins the completion race, the terminal result is
   `Cancelled`; if the operation already completed, its original result wins.
   A callback registration is a separate lifetime: closing it prevents future
   dispatch but does not interrupt a callback already running. A client must
   provide an explicit callback-quiescence point before captured state is
   destroyed.
5. **Retry safety:** retries are operation-specific. A read may retry when the
   source and policy say it is safe; a side-effecting procedure may retry only
   when its Lexicon/protocol semantics provide deduplication or an adapter
   contract explicitly guarantees that the caller-supplied deterministic
   operation identity is honored. Determinism by itself never proves
   idempotency, and uncertain side effects are reconciled rather than blindly
   repeated.
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
12. **Streams:** at-least-once is promised only for a replay-capable source,
    valid cursor/checkpoint, and a non-lossy backpressure policy. Live-only or
    explicitly lossy modes report gaps/drops and do not claim at-least-once.
    Consumers must tolerate duplicates and cursor expiry.
13. **Persistence optionality:** an in-memory implementation is a valid core
    configuration; durable backends are replaceable.
14. **Unknown data:** unknown fields and event variants do not cause unsafe
    memory access or credential disclosure; callers can inspect an explicit
    unknown representation where required.
