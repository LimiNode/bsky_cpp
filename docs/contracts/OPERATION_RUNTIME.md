# M1c operation runtime implementation contract

This describes the Draft implementation in
[`runtime.hpp`](../../src/bsky_cpp/runtime/runtime.hpp), constrained by
[ADR-0002](../adr/0002-async-execution-contract.md) and
[ADR-0004](../adr/0004-runtime-composition.md). It implements non-blocking
`Result<void>` work and one optional move-only terminal callback. It is a
runtime primitive, not a high-level Client or a network implementation.

## Admission, cancellation, and delivery

`start()` enables admission once. Zero capacity is `InvalidArgument`; restart
is `Conflict`. Validation or capacity rejection is synchronous and invokes no
callback. Registration, capacity accounting, queue posting, and closing
admission share one mutex, so shutdown cannot miss an admitted operation.
`Backpressure` means the configured number of operations is still retained
through work and callback cleanup, including queued and running delivery.

The first terminal contender wins. Explicit cancellation records `Cancelled`;
shutdown records `Shutdown`. Later work completion/cancellation cannot replace
that outcome. Cancellation does not preempt executing user work. A completed
handle observes status and optional failure category, not another payload.
Dropping a handle does not cancel an operation. Retained completed handles do
not consume capacity or retain callback captures.

Delivery always posts through the upstream task manager. If posting fails
after registration, the operation still returns an admitted handle, records
one terminal `Shutdown` dispatch failure, and closes the callback registration.
There is no inline callback fallback. The callback slot is reserved before the
terminal result is committed; failure to reserve it can therefore be reported
without rewriting a previously committed result. Work exceptions become
`Internal`; callback exceptions are contained after the terminal result is set.

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
Overlapping or reentrant processing fails with `Conflict`; it never enters the
upstream single-consumer task manager concurrently. Work and callbacks run on
that consumer. `next_deadline()` includes task and queued awaiter deadlines,
and returns now when immediate work exists. An optional shared notifier lease
in `RuntimeOptions` remains alive with the core. `notify()` must be short,
non-blocking, and must not reenter the runtime. Read the notifier generation
before processing and use its generation-aware wait after processing to avoid
lost wakeups. No borrowed notifier overload is provided.

Concurrent submit, cancellation, close, and shutdown are supported. Moving or
destroying a particular public object must not race ordinary access to that
same object. Execution methods take a local core lease, permitting runtime
destruction from inside an invocation on that executor.

## Stop and shutdown

`request_stop()` and destruction close admission, close pending callback
registrations, settle unfinished operations as `Shutdown`, and abort queued
work. They do not wait for executing user code. Shutdown uses this explicit
abort/suppression policy rather than executing arbitrary user callbacks on the
shutdown caller. An idle external runtime can finish cleanup with no further
external pass. An active external pass retains its state and finishes cleanup
when it returns; shutdown never starts a second consumer to drain it.

`shutdown(timeout)` succeeds only after work, callback captures, and the loop
are quiescent. A deadline failure is `ErrorCategory::Shutdown`, with pending
quiescence reported in its diagnostic message; the state remains alive and a
later shutdown can succeed. Self-shutdown returns pending immediately rather
than waiting on its own invocation. Repeated stop/shutdown is idempotent.
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
The managed snapshot is `2d4b09763603db2e5f9c5ca5bce7de3ffc9332b0`.
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
manager as a controlled executor for cancellation/completion order, callback
suppression, move-only and throwing callables, both post-admission rejection
paths, registry cleanup, and capacity. Gate-controlled threads test running
work/callback shutdown, concurrent admission/cancel/stop, overlapping external
passes, self-waits, and runtime destruction on its own managed/external callback.
Assertions fail the test process in Debug and Release. No sleep is used for
test synchronization. Installed consumers and package-isolation checks run in
CI together with runtime OFF/ON, Doxygen and Ubuntu ASan/UBSan.

Managed timed awaiters are not guaranteed on the pinned snapshot. The separate
[event-hub PR #13](https://github.com/LimiNode/event-hub-cpp/pull/13) must be
accepted and integrated before that claim. Explicit `ModuleHub::request_stop()`
already wakes its loop; the limitation concerns awaiter registration/deadline
aggregation, not that explicit wakeup.

Still unimplemented: typed value-result operations, injected public executors,
the blocking-transport bridge, operation deadlines, future adapters, high-level
Client, XRPC, kurlyk, OAuth, WebSocket, and persistence. M1c remains partial and
Draft pending independent source review; this contract does not claim that all
items in the broader async conformance plan are delivered. TSan is not part of
the current matrix; stress tests and ASan/UBSan do not substitute for it.
