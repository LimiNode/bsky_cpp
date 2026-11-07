# M1c operation runtime implementation contract

This describes the Draft implementation in
[`runtime.hpp`](../../src/bsky_cpp/runtime/runtime.hpp), constrained by
[ADR-0002](../adr/0002-async-execution-contract.md) and
[ADR-0004](../adr/0004-runtime-composition.md). It implements non-blocking
`Result<void>` work, typed `Result<T>` work, and one optional move-only
terminal callback per operation. It is a runtime primitive, not a high-level
Client or a network implementation. An admitted operation may also carry an
absolute monotonic deadline through `OperationOptions`.

## Admission, cancellation, and delivery

`start()` enables admission once. Zero capacity is `InvalidArgument`; restart
is `Conflict`. Validation or capacity rejection is synchronous and invokes no
callback. Startup may race with `request_stop()` or `shutdown()`: the runtime
serializes external `ModuleHub::initialize()` and `hub.shutdown()` through a
separate lifecycle gate. If stop wins before startup completes, startup
returns `Shutdown`, finalization happens after initialization reaches a safe
boundary, and no work is admitted. A stop after successful startup closes
admission normally. Registration and capacity accounting share the runtime
mutex; queue posting happens after that mutex is released, while the registered
state keeps shutdown from losing an admitted operation.
`Backpressure` means the configured number of operations is still retained
through work and callback cleanup, including queued and running delivery.

`OperationOptions::deadline` is an optional absolute
`std::chrono::steady_clock::time_point`. An already expired deadline is still
an admitted operation: it records `TimedOut`, requests cooperative
cancellation, skips user work, and dispatches the terminal callback
asynchronously. A deadline task uses the runtime's existing event-hub
`TaskManager`; it never starts a per-operation thread or a second scheduler.
If the deadline wins the terminal race, later success or failure cannot replace
it. If completion or explicit cancellation wins first, the timer is cancelled
or ignored and cannot request cancellation afterwards. A running work function
is not forcibly interrupted, so a deadline may be observed only when the
scheduler reaches its dispatch point after that function returns.

The first terminal contender wins. Explicit cancellation records `Cancelled`;
shutdown records `Shutdown`. Later work completion/cancellation cannot replace
that outcome. Cancellation does not preempt executing user work. A completed
handle observes status and optional failure category, not another payload.
Dropping a handle does not cancel an operation. Retained completed handles do
not consume capacity or retain callback captures.

`submit_typed<T>()` uses the same operation state, admission, cancellation,
shutdown, and callback-registration machinery as `submit()`. Its work returns
`Result<T>` and its callback receives exactly one move-only `Result<T>` when a
callback was supplied. A successful value is kept only until that callback is
delivered and is moved into it; it is never copied into or exposed by the
`OperationHandle`. `Result<T>` must be nothrow move constructible; this is a
compile-time boundary because a throwing payload move cannot be recovered
after the shared void lifecycle has committed terminal success. Payloads that
need fallible conversion must be normalized by the caller before returning
`Result<T>`. If no callback is supplied, the value is intentionally
discarded after terminal status is committed. Cancellation, shutdown, work
queue rejection, or a thrown typed work function produce a typed error in the
callback. Rejection by the callback executor happens after terminal
commitment: the callback is suppressed and the already selected outcome is
preserved. A losing typed success payload is discarded. `T=void` is not
accepted by `submit_typed`; `submit()` remains the void-operation API.

Typed work and typed callback closures have independent ownership. Closing a
queued registration destroys the typed callback and its captures immediately,
even when work remains queued or is still running; work captures remain until
that work returns. A callback that has started retains its captures until the
callback returns, just like a void-operation callback.

Delivery posts through `RuntimeOptions::executor` when supplied. If it is
empty, a private adapter posts through the existing `ModuleHub` task manager;
no second scheduler is created. If callback posting fails after registration,
the operation still returns an admitted handle, closes the callback
registration, and preserves the terminal outcome already committed. Rejection
of the admitted work task remains a terminal `Shutdown` outcome. An executor
that accepts and later drops a task is observed through the dispatch guard and
has the same suppression result. There is no inline callback fallback. The
callback slot is reserved before the terminal result is committed, and the
external `post()` call happens after releasing the core mutex. Work exceptions
become `Internal`; callback exceptions are contained after the terminal result
is set.
Terminalization is an exception boundary: allocation or dispatch failure after
admission enters an emergency terminal path, suppresses delivery when needed,
and still releases callback/work captures before registry retirement. The stop
and cancellation entry points are `noexcept`; they do not allocate a snapshot
of the registry, and their terminal-error construction has the same fallback.

`close()` suppresses a not-yet-started registration. An executing callback
finishes normally. `await_quiescence()` waits until delivery is completed or
suppressed **and callback captures have been released**, even if the handle is
retained. A call on this executor's own work/callback does not wait and reports
the currently observed state. Completed registrations are removed from the
runtime registry; it does not accumulate weak entries over time.

## Execution and lifetime

Managed mode runs the one upstream `ModuleHub::run()` loop on one execution
thread. Its shared core lease survives destruction of the public runtime.
There is no second scheduler, event queue, network worker, or cleanup thread.
Stopping the hub is deferred until running work and callbacks finish. The
loop releases its lease after upstream shutdown, avoiding self-join and a
permanent self-owned cycle.

External mode has one consumer calling `process_once()` and `next_deadline()`.
These calls are permitted only after external `start()` succeeds. Calls before
readiness, while startup is resolving a stop, or after admission closes are
rejected (`process_once()` returns `Conflict`; `next_deadline()` returns no
value). Overlapping or reentrant processing fails with `Conflict`; it never
enters the upstream single-consumer task manager concurrently. Work runs on
that consumer; callbacks run on the selected executor. `next_deadline()` includes
operation timers, task and queued awaiter deadlines,
and returns now when immediate work exists. An optional shared notifier lease
in `RuntimeOptions` remains alive with the core. `notify()` must be short,
non-blocking, and must not reenter the runtime. Read the notifier generation
before processing and use its generation-aware wait after processing to avoid
lost wakeups. Adding or cancelling an operation deadline wakes the configured
external notifier. No borrowed notifier overload is provided.

`RuntimeOptions::executor` is a shared lifetime lease for callback dispatch.
It accepts move-only tasks through `IExecutor::post()`, reports pre-admission
rejection with `false`, and must enqueue asynchronously. An accepted task must
run once or be destroyed; destruction is observable and suppresses the
registration. The runtime never owns or stops an injected executor. If its
queue remains open but does not drain, shutdown may time out; closing or
draining that queue is the application's responsibility. The default adapter
keeps the previous event-hub callback behavior. `post()` may be called
concurrently by completion threads. Executor implementations must not hold
their internal queue lock while invoking or destroying tasks, because task
destruction may run user capture destructors and reenter runtime state. An
executor may be destroyed on its callback thread and must not require a
self-join.

The finalization handoff is serialized by the core, consumer, and lifecycle
gates. Startup does not publish an accepting external runtime until upstream
initialization and the pending-stop decision are complete. A stop that arrives
while an external pass owns the consumer lease leaves finalization to that pass;
the pass rechecks the registry before handing the lease back. If no pass is
active, stop takes the consumer lease itself. `hub.shutdown()` is never called
concurrently with `hub.process_once()`. When callbacks run on an injected
executor, `shutdown()` performs this same final handoff after the callback
executor has made the registry empty; the injected executor is never used as a
second runtime consumer. Teardown takes locks in consumer, lifecycle, then
brief runtime-state order; upstream calls hold none of the runtime mutexes.

Concurrent submit, cancellation, close, and shutdown are supported. Moving or
destroying a particular public object must not race ordinary access to that
same object. Execution methods take a local core lease, permitting runtime
destruction from inside an invocation on that executor.

The allocation-free stop path marks operations one at a time while the core
mutex is released for user-capture cleanup. With the current bounded admission
model its worst-case registry traversal is O(n²) in `max_in_flight`; the normal
runtime limit is deliberately small, and the race/lifetime suite exercises the
configured bound. A future larger-capacity runtime should replace this with a
stable intrusive stop order rather than allocating from `noexcept` stop.

## Stop and shutdown

`request_stop()` and destruction close admission, close pending callback
registrations, settle unfinished operations as `Shutdown`, and abort queued
work. They do not wait for executing user code. Shutdown uses this explicit
abort/suppression policy rather than executing arbitrary user callbacks on the
shutdown caller. An idle external runtime can finish cleanup with no further
external pass. An active external pass retains its state and finishes cleanup
when it returns; shutdown never starts a second consumer to drain it.

`shutdown(timeout)` succeeds only after work, callback captures, and the loop
are quiescent. A self-shutdown returns `ErrorCategory::QuiescencePending`; a
wait that reaches its deadline returns `ErrorCategory::ShutdownTimedOut`. The
state remains alive and a later shutdown can succeed. Runtime stop itself and
unfinished operation outcomes continue to use `ErrorCategory::Shutdown`.
Repeated stop/shutdown is idempotent.
Running user code cannot be forcibly stopped. Work, notifier calls, callbacks,
and capture destructors must remain short; deadlines cannot preempt a blocking
capture destructor executed by the caller during closure.

## Package and compatibility

The `runtime/` source domain is introduced by this Draft implementation under
ADR-0004. The foundation umbrella stays usable with no event-hub includes.
`BSKY_PUBLIC_HEADERS` includes the selected runtime header when runtime is
enabled and generates installation, documentation, and self-containment tests.
All private fixtures remain under `runtime/detail/` and are not installed.

`BSKY_BUILD_RUNTIME` defaults to `OFF`. With it enabled, CMake first reuses an
existing or installed `event_hub::event_hub` target; otherwise it composes the
pinned direct submodule once, with upstream tests/examples/Time Shield/NTP
disabled in function-local option scope (no forced parent cache changes).
The managed snapshot is `e3580057a0273e7004cd4fd91ad9ae7d74d4c5b3`, the merge
commit for upstream event-hub-cpp PR #13.
Event-hub uses the MIT license, has no required network dependency, and its
reviewed API remains behind the runtime boundary. Replacing it requires
revalidating scheduler/lifecycle conformance and the exported runtime target.

Installed consumers request runtime explicitly:

```cmake
find_package(bsky_cpp CONFIG REQUIRED COMPONENTS runtime)
target_link_libraries(application PRIVATE bsky_cpp::runtime)
```

The no-component `find_package(bsky_cpp CONFIG REQUIRED)` imports foundation
only. It does not discover event-hub even when runtime is installed beside it.
Runtime is in a separate export set and alone invokes the transitive
`find_dependency(event-hub-cpp CONFIG)`. This is a migration from the initial
Draft's implicit runtime import; foundation API names and includes are unchanged.
`Backpressure` is appended to the category enum to preserve existing ordinals.

## Evidence and remaining gates

[`tests/runtime.cpp`](../../tests/runtime.cpp) uses the real external task
manager and a deterministic move-only fake executor for
cancellation/completion order, callback suppression, typed success/error
delivery, move-only and throwing callables, callback-executor rejection and
close-after-acceptance, typed callback lifetime, terminalization faults,
registry cleanup, and capacity. The fake covers accepted, rejected, queued,
running, closed, drained, and suppressed states. A private admission gate
makes the producer/stop boundary deterministic without adding a production
hook.
Deadline cases cover already-expired admission, external and managed
dispatch, cancellation and completion races, typed timeout delivery,
callback suppression, capacity reuse, and the cooperative no-preemption rule.
Gate-controlled threads test running
typed work/cancellation, running work/callback shutdown, concurrent
admission/cancel/stop, overlapping external passes, self-waits, and runtime
destruction on its own managed/external callback. Startup gates cover
stop-before-start, stop during external initialization, managed startup races,
and repeated concurrent shutdown without a second hub consumer.
Assertions fail the test process in Debug and Release. Lifecycle tests use
handshakes rather than sleeps; scheduler integration cases use only bounded
steady-clock waits. Installed consumers and package-isolation checks run in CI
together with runtime OFF/ON, Doxygen and Ubuntu ASan/UBSan.

Managed timed awaiters are supported by the pinned snapshot. Upstream PR #13
adds queued awaiter deadline aggregation to `ModuleHub` and notifies the loop
after awaiter registration, including when a newly registered deadline is
earlier than the current wait. Explicit `ModuleHub::request_stop()` continues
to wake its loop.

Still unimplemented: the blocking-transport bridge, future adapters,
high-level Client, XRPC, kurlyk, OAuth, WebSocket, and persistence. M1c remains
partial and Draft pending independent source review;
this contract does not claim that all items in the broader async conformance
plan are delivered. TSan is run as a
separate Linux job when supported by the toolchain; stress tests and ASan/UBSan
do not substitute for it.
