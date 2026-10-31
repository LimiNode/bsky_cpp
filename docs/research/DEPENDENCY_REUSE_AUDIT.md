# Stage 2 dependency reuse audit

**Status:** research-only, Draft milestone material. No dependency is added to
`external/` by this document and no upstream source is modified.

This audit records what was verified from exact upstream snapshots before any
M1c implementation. Paths and line numbers refer to the detached source trees
under `build/dependency-audit/`; the SHA is part of the evidence and must be
rechecked when a dependency is updated.

| Repository | Exact audited commit | Proposed owner | Current decision |
| --- | --- | --- | --- |
| `event-hub-cpp` | `2d4b09763603db2e5f9c5ca5bce7de3ffc9332b0` | upstream project, with a bsky adapter | reuse runtime primitives through a thin adapter; defer dependency adoption until the runtime ADR is implemented |
| `kurlyk` | `b62cd53f672c47b74bb14d762ea9dc59a9df19ce` | upstream project, with a bsky transport adapter | reuse native async path; keep the public seam transport-neutral; defer OAuth profile work |
| `time-shield-cpp` | `d5f1203f24341e4146af35d5bb36e762056d181f` | upstream project, with a bsky time adapter | reuse clock/calendar primitives after conformance gate; propose upstream parser fix |

The words “reuse” and “adapter” below are design decisions, not evidence that
the corresponding integration exists in `bsky_cpp`. M1c remains unimplemented.

## Evidence and capability matrix

### event-hub-cpp

| Capability | Existing API and source evidence | Decision | Missing guarantee / test | Owner / milestone |
| --- | --- | --- | --- | --- |
| Hub lifecycle | `ModuleHub::run()` and `start()` initialize modules, run a blocking or background loop, shut down, and rethrow background exceptions (`event-hub-cpp/include/event_hub/module_hub.hpp:279-325`, SHA above). `ModuleExecutionMode` provides `inline_in_hub`, `private_thread`, and `manual` (`module.hpp:26-31`). | Reuse directly through a thin `RuntimeAdapter`; expose managed mode only after M1c. | Need a deterministic test for client destruction, shutdown deadline, and callback quiescence across all module modes. | upstream + bsky adapter, M1c |
| External/manual execution | `ModuleHub::process_once()` delegates to `process()` (`module_hub.hpp:201-204`); `set_notifier()` wires a non-owning notifier (`module_hub.hpp:264-270`). `RunLoop::process_once()` and `run_until()` aggregate buses and task managers and wait on notifier/deadlines (`run_loop.hpp:129-186`). | Thin adapter in external mode; caller drives `process_once()` and uses a notifier plus deadline hint. | Adapter must not retain a borrowed notifier past its documented lifetime; add a fake-clock/manual-loop test. | bsky adapter, M1c |
| Tasks, delay, periodic work | `TaskManager` supports ready/delayed/periodic tasks, `post_every_after`, `cancel`, `next_deadline`, and `TaskContext` cancellation/reschedule (`task_manager.hpp:199-275`, `535-607`, `737-771`, `988-1077`). Periodic scheduling is fixed-delay or fixed-rate (`task.hpp:95-117`). | Reuse directly; do not implement a second task manager. | `TaskManager` is single-consumer for `process()` and has no queue capacity. Test admission/backpressure at the bsky boundary, not by assuming an upstream bound. | upstream + bsky policy, M1c |
| Priorities and fairness | Three FIFO ready queues are selected high, normal, low (`task.hpp:95-105`; `task_manager.hpp:1120-1129`). | Reuse priority levels as an implementation detail only. | No starvation/fairness guarantee is documented. Add a bounded mixed-priority deterministic test before promising fairness. | bsky conformance, M1c |
| Task exceptions | `TaskManager::set_exception_handler()` either reports and continues or, without a handler, restores unstarted work and rethrows (`task_manager.hpp:86-92`, `610-673`). | Thin adapter maps an exception to one terminal operation outcome. | Verify callback exceptions cannot create a second terminal `Result`; test handler failure policy. | bsky adapter, M1c |
| Event delivery | `EventBus` offers direct and queued delivery policies; callbacks run on the thread calling `emit_direct()` or `process()` (`event_bus.hpp:73-88`, `457-531`). `EventEndpoint` owns subscriptions/awaiters and closes them on destruction (`event_endpoint.hpp:35-84`). | Reuse directly for internal runtime signals; do not expose EventBus types in high-level DTOs. | Queued events are held in an unbounded `std::queue` (`event_bus.hpp:525-565`, `839-840`). A bsky operation queue must impose explicit bounds and typed backpressure. | bsky adapter, M1c |
| Awaiters and cancellation | `EventAwaiter` polls cancellation and timeout cooperatively and exposes `next_deadline()` (`event_awaiter.hpp:25-30`, `140-209`). `EventBus::next_awaiter_deadline()` aggregates active awaiters (`event_bus.hpp:570-593`). | Reuse awaiters for internal coordination only. | Define which cancellation source owns an operation and prove callback quiescence; add close-before-start and timeout race tests. | bsky conformance, M1c |
| Run-loop deadline handling | `RunLoop::next_task_wait()` includes queued EventBus awaiter deadlines and task deadlines (`run_loop.hpp:224-258`). | Prefer `RunLoop` or equivalent explicit aggregation in external mode. | `ModuleHub::next_deadline()` only visits module task managers and module hints (`module_hub.hpp:243-261`); it does **not** include `m_bus.next_awaiter_deadline()`. | upstream issue proposal; bsky regression, M1c |
| Calendar scheduling / Time Shield | `CalendarScheduler` delegates to an existing `TaskManager`, owns no thread, and documents task-manager lifetime (`calendar_scheduler.hpp:30-73`). Tests cover DST, overlap, and exception behavior (`tests/test_calendar_scheduler.cpp:475-518`). | Reuse only for calendar semantics; use the hub scheduler as the execution owner. | Verify timezone fixture set used by AT Protocol jobs; no second scheduler. | upstream + bsky adapter, later milestone |
| Shutdown and worker lifetime | `Module::shutdown()` stops/join workers, closes tasks and endpoint; it explicitly does not wait for EventBus callbacks already started (`module.hpp:130-180`). `ModuleHub::shutdown()` coordinates modules. | Treat module shutdown and operation callback quiescence as separate phases. | A bsky shutdown deadline must report `QuiescencePending`/`ShutdownTimedOut`, not silently claim quiescence. | bsky operation state, M1c |
| Backpressure | TaskManager and EventBus expose counts but no bounded queue admission. | Implement bsky operation admission bounds above upstream. | Deterministic queue-limit test; never infer bounded allocation from `process()` quotas. | bsky, M1c |

#### Managed and external modes

Managed mode means the adapter owns a `ModuleHub` lifetime lease and calls
`start()`/`run()` according to the selected thread policy. External mode means
the application owns the loop: the adapter installs a notifier, calls
`process_once()`, and computes a wait from all task, event-awaiter, and transport
deadlines. The adapter must not build a parallel `TaskManager` or client loop:
that would duplicate cancellation, wake-up, priority, exception, and shutdown
semantics already implemented upstream.

The `ModuleHub::next_deadline()` omission is an upstream compatibility risk, not
a local patch target. The minimum deterministic regression test is:

1. create a hub with an inline module and a queued event-bus awaiter with a
   short timeout;
2. assert `hub.next_deadline()` is either the awaiter deadline or, if the
   upstream contract intentionally excludes it, that the external adapter uses
   `EventBus::next_awaiter_deadline()` separately;
3. drive the loop with no queued event and assert the awaiter expires before a
   larger idle cap.

Until upstream clarifies or fixes this, external mode must aggregate the bus
deadline explicitly and managed mode must use a bounded wake-up cap. No
`event-hub-cpp` change is included in this repository.

### kurlyk

| Capability | Existing API and source evidence | Decision | Missing guarantee / test | Owner / milestone |
| --- | --- | --- | --- | --- |
| Native async worker | `NetworkWorker` is a singleton with `start(bool)`, `process()`, notifier wake-up, and `stop()` waiting on a shared future (`core/NetworkWorker.hpp:91-121`, `133-210`). | Reuse the native async path; do not create a second network worker. | Verify worker startup/shutdown ordering and that no bsky callback blocks the worker. | kurlyk adapter, M1c/M1d |
| Admission and terminal forwarding | `HttpClient::submit_request()` performs a serialized per-client `max_in_flight` check and returns `SubmitResult`; accepted work wakes `NetworkWorker` (`http/HttpClient.hpp:132-156`, `581-607`). `SubmitResult` separates `accepted` and `error_code` (`types/SubmitResult.hpp:12-30`). | Map admission rejection synchronously to bsky `Result<OperationHandle>`; map accepted completion once into operation state. | Direct `HttpRequestManager` producers bypass the client cap (`HttpClient.hpp:132-138`). Add admission, transport failure, terminal response, and `OutcomeUnknown` tests. | bsky adapter, M1c/M1d |
| In-flight limits | `set_max_in_flight()` is per `HttpClient`, serialized only for that instance (`HttpClient.hpp:132-142`). | Use one client/group per bsky policy domain and document scope. | No global invariant across producers; test queue-limit behavior under concurrent callers. | bsky policy, M1c |
| Cancellation and destruction | `HttpClient::~HttpClient()` calls blocking `cancel_requests()`; destructor warns it must not run on worker thread (`HttpClient.hpp:39-52`). `cancel_requests()` waits for a callback from a future (`:52-85`). | Keep client lifetime in the adapter lease; never destroy it from a kurlyk callback. | Add a deterministic destructor-on-worker negative test and bounded shutdown timeout. Blocking destruction cannot be hidden in bsky callbacks. | bsky adapter, M1c |
| Rate limits and partitions | RAII `HttpRateLimitHandle` keeps entries alive; `HttpRateLimiter` supports general/specific handles, partition keys, sequential in-flight tokens, and release on completion (`http/HttpRequestManager/HttpRateLimiter.hpp:15-22`, `118-249`; `HttpClient.hpp:337-366`). | Reuse kurlyk rate limiting; do not duplicate token buckets in bsky. | Decide global vs per-account partition keys and test retry token release after cancellation/destruction. | bsky policy, M1d |
| Retries and status policy | Request configuration has retry attempts/delay, timeout, connect timeout, redirects, and streaming (`http/data/HttpRequest.hpp:39-59`, `211-286`; `HttpClient.hpp:419-506`). | Keep transport retries in kurlyk; operation-specific retry safety remains in bsky. | Verify valid-status and XRPC error mapping before retry; side-effecting operations must not blindly retry. | bsky adapter, M1d |
| Streaming | `HttpResponse` distinguishes final `ready` responses from intermediate `stream_chunk` responses and carries ordered body/status/error metadata (`http/data/HttpResponse.hpp:10-40`). | Thin adapter forwards chunks to a separate stream contract; no one-shot operation completion per chunk. | Test cancellation during streaming and terminal callback exactly once. | bsky adapter, later streaming milestone |
| Headers and diagnostics | Headers are a case-insensitive unordered multimap (`utils/CaseInsensitiveMultimap.hpp:52-70`); response metadata includes status, headers, errors, retry attempt, and timing (`http/data/HttpResponse.hpp:17-32`). | Convert to bsky owned ordered header entries; preserve repeated-field order while adapting. | The upstream container itself does not promise wire order. Add repeated-header fixture and redaction tests. | bsky adapter, M1d |
| WebSocket/reconnect | NetworkWorker also processes registered network managers (`NetworkWorker.hpp:105-121`); this audit did not establish a complete WebSocket reconnect/backpressure contract from the pinned headers. | Defer WebSocket adapter. | Need explicit reconnect, queue bound, close, and replay policy before M4. | defer, M4 |
| Auth helpers | `OAuthPkceClient` implements generic authorization-code/PKCE, token exchange and refresh (`http/auth/OAuthPkceClient.hpp:26-214`). | Reuse only as a low-level helper if needed; do not call it AT Protocol OAuth. | Missing PAR, issuer/metadata discovery, DPoP, scope and identity verification. Add no public OAuth claim. | defer, M4 |

`future.get()` must not be used from an event-loop or kurlyk worker callback.
The adapter should forward native completion into the operation state machine.
Admission failure, network failure, an HTTP/XRPC terminal response, and an
uncertain side effect are distinct outcomes even when kurlyk supplies one
`HttpResponse` object.

### time-shield-cpp

| Capability | Existing API and source evidence | Decision | Missing guarantee / test | Owner / milestone |
| --- | --- | --- | --- | --- |
| ISO-8601 parsing | `parse_iso8601` documents 1–9 fractional digits (`text/time_parser.hpp:997-1011`), but `parse_fraction_to_ms()` rejects the fourth digit (`:556-584`, especially `:567-570`). Existing tests cover only `.789` (`tests/parse_iso8601_test.cpp:76-80`). | Reuse after a conformance gate; preserve source text in a Lexicon datetime wrapper. | Upstream corrective proposal: accept 1–9 digits, define precision/rounding, and add 1/3/6/9-digit valid plus >9 invalid tests. | upstream proposal + bsky wrapper, M1d |
| DateTime and offsets | `DateTime` stores UTC milliseconds plus a fixed offset, parses and formats ISO-8601 (`datetime/DateTime.hpp:23-25`, `156-204`, `659-698`). | Thin adapter for calendar values; do not use millisecond storage as a lossless Lexicon representation. | A datetime round-trip must retain the original fractional precision/string when canonical record bytes depend on it. | bsky, M1d |
| Named zones | `ZonedClock` resolves named zones/fixed offsets for a UTC instant and optionally uses NTP (`timezone/ZonedClock.hpp:25-30`, `159-186`). | Reuse for presentation/calendar scheduling only. | AT Protocol timestamps are protocol strings; timezone conversion must not rewrite record content silently. | bsky, M1d |
| Monotonic deadlines | `TimerScheduler::TimerClock` is `std::chrono::steady_clock` (`timers/TimerScheduler.hpp:40-43`); timers can be manually processed or run on a worker (`:114-124`, `249-341`). | Use event-hub task deadlines for runtime; use Time Shield monotonic helpers only at an adapter boundary. | No second scheduler: test deadline conversion against one injected steady clock. | bsky/event-hub adapter, M1c |
| Calendar helpers | Calendar and timezone helpers are useful for user-facing schedules. | Defer to calendar milestone; do not couple operation timeout logic to wall time. | DST fold/gap and leap-second policy remain open. | bsky, later milestone |
| NTP | The NTP service has process-lifetime singleton/shutdown behavior (`ntp/ntp_time_service.hpp:195-205`, `303-307`). | Do not include NTP in core or operation deadlines. | Requires an explicit clock-trust policy, network access, and shutdown owner. | defer |

#### Upstream corrective proposal (not implemented here)

For `time-shield-cpp` commit `d5f1203…`, add tests for
`2024-07-08T12:34:56.1Z`, `.123Z`, `.123456Z`, and `.123456789Z`, and an
invalid ten-digit fraction. Decide whether the value type remains millisecond
precision (truncation must be documented) or gains a finer representation.
The bsky adapter must retain the original Lexicon datetime string and use a
parsed instant only for validation/comparison; it must not silently canonicalize
record content through `DateTime::to_iso8601_ms()`.

## Cross-cutting ownership and unresolved questions

- **Verified behavior:** all observations in the matrices were read from the
  pinned detached trees and their local tests at the SHAs in the opening
  table; no behavior is inferred from a package name or a README promise.
- **Assumptions for the proposed composition:** one operation runtime has one
  event-loop owner; a transport adapter can forward completion without
  blocking that owner; and the application can provide an external notifier
  whose lifetime covers every producer. These assumptions must become explicit
  constructor/API requirements in M1c or be rejected at admission.
- `event-hub-cpp` owns scheduling, event dispatch, and notifier mechanics;
  `bsky_cpp` owns operation admission, typed outcomes, callback registration,
  queue bounds, and shutdown/quiescence policy.
- `kurlyk` owns HTTP I/O, transport retries, and rate-limit token accounting;
  `bsky_cpp` owns protocol retry safety, XRPC mapping, and `OutcomeUnknown`.
- `time-shield-cpp` owns parsing/formatting primitives; `bsky_cpp` owns
  protocol-preserving datetime representation and monotonic deadline policy.
- Should the upstream `ModuleHub` aggregate bus awaiter deadlines in a future
  release, or should the adapter keep explicit aggregation for compatibility?
- What queue and fairness guarantees can each production integration support?
- Which per-account key is stable enough for kurlyk rate-limit partitions while
  avoiding credential or identity leakage?
- Which exact AT Protocol OAuth profile and upstream fixture version will gate
  M4 authentication?

## Required conformance matrix for M1c

Before adopting any optional dependency, add deterministic tests for:

1. managed and external execution with one injected fake clock;
2. notifier wake-up and every deadline source (tasks, awaiters, transport);
3. admission rejection versus post-admission terminal rejection;
4. cancellation/completion first-winner behavior and callback suppression;
5. queue limits, rate-limit partition isolation, and retry token release;
6. client destruction without use-after-free or worker-thread blocking;
7. repeated response-header order and diagnostic redaction;
8. 1–9 digit datetime fixtures and exact Lexicon string round-trip.

No item in this matrix is implemented by this audit.
