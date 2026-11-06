# ADR-0004: Runtime composition and dependency ownership

- **Status:** Accepted direction; a partial M1c Draft implementation is
  available behind the opt-in `BSKY_BUILD_RUNTIME` CMake option.
- **Date:** 2026-10-31
- **Depends on:** ADR-0002, ADR-0003, and [`DEPENDENCY_REUSE_AUDIT.md`](../research/DEPENDENCY_REUSE_AUDIT.md)

## Context

Stage 2 audited exact upstream snapshots of `event-hub-cpp`, `kurlyk`, and
`time-shield-cpp`. The foundation/value layer must remain dependency-independent,
but a production asynchronous Client needs one coherent scheduler, event
orchestration policy, and operation state machine. The accepted direction is to
use `event-hub-cpp` as the required infrastructure of the production operation
runtime, while keeping the high-level Client transport-neutral and network,
persistence, and application-level streams behind replaceable or optional
components.

The foundation remains dependency-independent. The M1c runtime target and its
event-hub composition are implementation evidence for the operation-runtime
portion of this decision. The pinned event-hub snapshot now includes the
reviewed awaiter deadline and wake-up correction.

The Draft's exact subset, source domain, package component selection, stop
policy, lifetime ownership, and compatibility implications are specified in
[OPERATION_RUNTIME.md](../contracts/OPERATION_RUNTIME.md). This implementation
uses the upstream blocking `ModuleHub::run()` on its sole leased execution
thread, rather than `start()`'s internally joinable thread. This allows a
callback to release the last public owner without self-join; no cleanup worker
or additional scheduler is introduced. The broader M1c gate remains open.

## Decision

### Dependency boundary and future CMake topology

The future package topology is intentionally explicit:

| Component | Target | Dependency and role | Installation |
| --- | --- | --- | --- |
| Foundation | `bsky_cpp::bsky_cpp` | Current value/protocol/transport-neutral primitives | Default component; no optional dependency |
| Production runtime | `bsky_cpp::runtime` | Requires `event-hub-cpp::event-hub-cpp` and Threads; owns scheduling/orchestration composition | Opt-in component; installed config calls `find_dependency(event-hub-cpp CONFIG)` |
| High-level Client API | provisional `bsky_cpp::client` | Requires the production runtime and an abstract async transport; no concrete network backend | Installable without kurlyk when another compatible transport is selected |
| Kurlyk transport adapter | provisional `bsky_cpp::kurlyk_adapter` | Concrete backend for the runtime; requires the pinned kurlyk package and forwards native completion without exposing kurlyk DTOs | Opt-in adapter component; only this component discovers/exports kurlyk |
| Other transport adapters | future transport-specific targets | Implement the same abstract async transport seam without changing Client | Independent opt-in components |
| Streams | future `bsky_cpp::streams` | Optional HTTP/WebSocket source and bsky replay/backpressure contract | Opt-in; not required by one-shot Client operations |
| Persistence | future `bsky_cpp::persistence` | Optional MDBX-backed storage | Opt-in |

The foundation target continues to configure, build, install, and test without
initialized submodules. Dependency direction is one-way: foundation has no
runtime or network dependency; runtime depends on `event-hub-cpp`; the
transport-neutral Client depends on runtime plus the abstract transport seam;
each concrete adapter depends on the Client/runtime seam and only its own
backend. The runtime target is the one place where `event-hub-cpp` becomes
mandatory for production operation execution. Application event subscriptions
are not the same as runtime infrastructure and remain optional at the public
API boundary.

`find_dependency(kurlyk CONFIG)` belongs only in the installed kurlyk adapter
component. The common SDK package and a Client built with another compatible
transport must not require kurlyk.

The implementation PR must prefer installed package targets over nested
`add_subdirectory()` calls. If source composition is unavoidable, it must add
each pinned dependency once, avoid duplicate physical Time Shield copies, set
Time Shield NTP explicitly `OFF`, and isolate upstream cache variables in a
documented preset/toolchain. Installed package configs must use
`find_dependency()` for every transitive target they expose. The audited
event-hub CMake currently uses `add_subdirectory()` for Time Shield and `FORCE`
for several cache options, so this is a required integration constraint rather
than an implementation detail.

### Managed and external execution

The production runtime supports two modes over the same event-hub primitives:

- **Managed:** the runtime owns an event-hub `ModuleHub` lifetime lease and
  starts/stops it according to an explicit policy. Upstream modules own their
  task managers and private workers. The pinned upstream snapshot aggregates
  queued awaiter deadlines and wakes the loop after awaiter registration, so
  managed timed awaiter delivery and timeout-driven cleanup are covered.
- **External:** the caller owns the loop. The runtime installs a non-owning
  notifier, calls `process_once()`, and explicitly aggregates module task,
  EventBus awaiter, and transport deadlines. It must not create a parallel
  TaskManager, EventBus, or client run loop.

Callback dispatch is independently injectable through the shared `IExecutor`
lease defined by ADR-0005. The event-hub `ModuleHub` remains the only work
scheduler; an injected executor changes callback placement only.

The pinned `ModuleHub::next_deadline()` aggregates queued
`EventBus::next_awaiter_deadline()` values, and `EventBus::register_awaiter()`
notifies after releasing `m_awaiters_mutex`. The upstream tests cover managed
`run()`/`start()` and an external notifier-driven loop, including a newly
earlier deadline. External mode still owns its loop and must use the notifier
and deadline contract; it does not create a parallel scheduler.

### Ownership, cancellation, shutdown, and callbacks

The bsky operation state machine owns the first-winner terminal outcome. Each
accepted operation retains shared state and the runtime/executor lease through
terminal completion and callback quiescence. A borrowed external runtime is
accepted only with an explicit caller lifetime guarantee. Destroying `Client`
stops admission and initiates shutdown without invalidating operation state or
callback captures.

Transport cancellation is cooperative and may race with completion. Callback
registration closes independently: close-before-start suppresses a queued
callback, while an already-running callback may finish. Quiescence includes
queued registrations that are delivered or suppressed and every in-flight
callback. Shutdown reports `QuiescencePending` or `ShutdownTimedOut` when its
deadline expires.

The callback-first `OperationHandle` contract in ADR-0002 is unchanged. Any
change to callback-first delivery, handle ownership, or the mutually exclusive
future adapter requires a new ADR.

`kurlyk::NetworkWorker` is reused for native async HTTP. A second network worker
or per-request waiter thread is prohibited. Because `kurlyk::HttpClient`
destruction blocks while group cancellation callbacks are delivered, the
runtime must retain a process-level network-worker lease and must never destroy
the last client from a kurlyk worker callback. Closing one bsky Client must not
stop infrastructure used by another Client.

### Transport completion, retries, and rate limits

The future kurlyk adapter forwards native completion directly into operation
state; it never calls `future.get()` from an event loop or callback. Admission
failure is synchronous and produces no callback. Rejection of admitted work is
a terminal typed error; rejection by a callback executor after terminal
commitment suppresses delivery without rewriting the selected outcome. Kurlyk
executes only retries
authorized by bsky's operation/idempotency policy. Unsafe side effects disable
automatic retry unless protocol-validated idempotency is available, and an
uncertain side effect may still produce `OutcomeUnknown`.

Kurlyk owns wire retries, rate-limit handles, sequential tokens, and partition
state. bsky owns XRPC mapping, retry safety, and the meaning of terminal
outcomes; it must not build a second token bucket.

Synthetic transport statuses are diagnostic signals, not protocol success:
`CURLE_OPERATION_TIMEDOUT` and explicit cancellation may surface as `499`, while
other curl errors without an HTTP status may surface as `451`. A future adapter
must inspect `HttpResponse::error_code`, cancellation state, and transport
completion context; HTTP status alone is insufficient to classify the outcome.
M1d conformance must cover these mappings separately.

### Clock and test policy

The pinned event-hub TaskManager and EventAwaiter call
`std::chrono::steady_clock::now()` directly and expose no clock-provider API.
M1c therefore separates:

1. deterministic operation-state tests with a fake executor and controlled
   completion/cancellation events;
2. scheduler integration tests using real monotonic deadlines and bounded waits;
3. an optional upstream improvement for injectable clock/time providers if
   full virtual-time scheduler testing is required.

No conformance claim may depend on a nonexistent fake-clock hook, tight sleeps,
or unbounded polling.

## Upstream corrective proposals

### event-hub-cpp: awaiter deadline aggregation and wake-up (resolved)

The correction was accepted in upstream PR #13 and is pinned at merge commit
`e3580057a0273e7004cd4fd91ad9ae7d74d4c5b3`. It:

1. aggregate queued/compatible `EventBus::next_awaiter_deadline()` values in
   `ModuleHub::next_deadline()`;
2. notify the loop when `EventBus::register_awaiter()` adds new waiting work,
   when its deadline is earlier than the current one, or when the loop was
   previously waiting with no deadline;
3. perform that notification after releasing `m_awaiters_mutex` (the notifier
   must never be called while that mutex is held), preserving lock ordering;
4. cover an awaiter registered before loop start and one registered after the
   loop has entered its wait state;
5. add deterministic tests for `ModuleHub::start()`, blocking `run()`, and an
   external notifier-driven `process_once()` loop;
6. assert bounded shutdown/join and no lost wake-ups in every scenario.

Tests should use handshakes/barriers and bounded deadlines rather than tight
sleeps or iteration-count polling. The bsky integration consumes this upstream
implementation without copying its EventBus or ModuleHub code.

### time-shield-cpp: fractional-second contract

The parser documents 1–9 fractional digits but rejects the fourth digit. The
upstream correction must define whether values are retained at nanosecond
precision or deliberately truncated/rounded to milliseconds, then add positive
fixtures for 1/3/6/9 digits and a negative ten-digit fixture. Until released,
bsky preserves the original Lexicon datetime string and does not round-trip
through a lossy millisecond formatter.

## Milestone-scoped conformance gates

### M1c — Operation Runtime

Required: managed/external composition with the pinned managed timed-awaiter
correction, bounded operation admission, synchronous
pre-admission rejection,
exactly-one terminal outcome, cancellation/completion first-winner behavior,
callback closure/suppression, callback-first typed `Result<T>` delivery with
move-only payloads, move-only callbacks, post-admission executor rejection,
shutdown/quiescence, lifetime leases, destruction safety, exception containment,
and deterministic fake execution/race tests.

M1c must not require kurlyk, XRPC, WebSocket, OAuth, persistence, or Time
Shield Lexicon integration. The foundation remains dependency-independent.

### M1d — Native Transport/XRPC Integration

Required when implemented: native kurlyk completion forwarding, HTTP/status/XRPC
mapping, retry/idempotency semantics, rate-limit partitions and token release,
response-header conversion, transport cancellation, XRPC encoding/decoding,
timestamp validation, Lexicon string preservation, installed consumer checks,
and dependency export checks.

### M4 — Advanced Infrastructure

Required when implemented: WebSocket reconnect, Jetstream replay/checkpoints/
gaps, persistent recovery, AT Protocol OAuth, streaming backpressure, and
network-specific reconciliation.

## Consequences

This composition reuses tested upstream scheduling and HTTP primitives without
making them mandatory for foundation consumers. It gives the production Client
one runtime owner, prevents duplicate workers/schedulers, and makes package
installation consequences explicit. Managed awaiter deadlines are now covered
by the pinned upstream correction, while typed M1c operation semantics continue
with fake execution and bounded real-clock integration tests.

The existing M1b synchronous `Transport::send()` seam remains valid and is not
replaced by this ADR. It is a low-level fake-friendly seam, not the application
async implementation.

## Open questions

- Which event-hub release policy should replace the pinned merge commit when a
  compatible tagged release is available?
- What queue bounds and fairness policy are appropriate for one-shot operations
  versus streams?
- Which ordered-header representation should kurlyk expose upstream, or should
  bsky explicitly weaken its wire-order contract?
- Which official AT Protocol OAuth profile and fixture revision will gate M4?
