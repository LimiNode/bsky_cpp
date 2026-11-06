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
Overlapping or reentrant processing fails with `Conflict`; it never enters the
upstream single-consumer task manager concurrently. Work and callbacks run on
that consumer. `next_deadline()` includes task and queued awaiter deadlines,
and returns now when immediate work exists. An optional shared notifier lease
in `RuntimeOptions` remains alive with the core. `notify()` must be short,
non-blocking, and must not reenter the runtime. Read the notifier generation
before processing and use its generation-aware wait after processing to avoid
lost wakeups. No borrowed notifier overload is provided.

The finalization handoff is serialized by the core mutex. A stop that arrives
while an external pass owns the consumer lease leaves finalization to that pass;
the pass rechecks the registry before handing the lease back. If no pass is
active, stop takes the consumer lease itself. `hub.shutdown()` is never called
concurrently with `hub.process_once()`.

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
manager as a controlled executor for cancellation/completion order, callback
suppression, move-only and throwing callables, both post-admission rejection
paths, registry cleanup, and capacity. Gate-controlled threads test running
work/callback shutdown, concurrent admission/cancel/stop, overlapping external
passes, self-waits, and runtime destruction on its own managed/external callback.
Assertions fail the test process in Debug and Release. No sleep is used for
test synchronization. Installed consumers and package-isolation checks run in
CI together with runtime OFF/ON, Doxygen and Ubuntu ASan/UBSan.

Managed timed awaiters are supported by the pinned snapshot. Upstream PR #13
adds queued awaiter deadline aggregation to `ModuleHub` and notifies the loop
after awaiter registration, including when a newly registered deadline is
earlier than the current wait. Explicit `ModuleHub::request_stop()` continues
to wake its loop.

Still unimplemented: typed value-result operations, injected public executors,
the blocking-transport bridge, operation deadlines, future adapters, high-level
Client, XRPC, kurlyk, OAuth, WebSocket, and persistence. M1c remains partial and
Draft pending independent source review; this contract does not claim that all
items in the broader async conformance plan are delivered. TSan is run as a
separate Linux job when supported by the toolchain; stress tests and ASan/UBSan
do not substitute for it.
