# ADR-0005: Callback executor abstraction

- **Status:** Draft M1c implementation
- **Date:** 2026-11-06
- **Depends on:** [ADR-0002](0002-async-execution-contract.md) and
  [ADR-0004](0004-runtime-composition.md)

## Context

The operation runtime needs a deterministic callback-dispatch seam without
giving every application a second scheduler. `event-hub-cpp` remains the
runtime's work scheduler and owns the only `ModuleHub` task manager. Tests and
applications may still need callback delivery on another event loop, so the
callback channel must be injectable and must accept move-only tasks.

## Decision

`bsky_cpp::runtime::IExecutor` is a small public callback-dispatch interface:

```cpp
virtual bool post(MoveOnlyFunction<void()> task) = 0;
```

The `RuntimeOptions::executor` field is a `std::shared_ptr<IExecutor>`. This is
an explicit lifetime lease: the runtime and every admitted callback state keep
the executor object alive until the registration is delivered or suppressed
and its captures have been destroyed. No borrowed-executor overload is
provided in this milestone.

`post()` has three required rules:

1. it takes ownership of a move-only task and returns `true` only after
   admission;
2. it enqueues asynchronously and never invokes the task inline;
3. an admitted task is eventually invoked once or destroyed. Destruction is
   observable to the runtime through a private dispatch guard and suppresses
   the callback, so a closed queue cannot leave an operation waiting forever.

An exception from `post()` is treated as rejection. Rejection before callback
admission never runs a callback. If terminal state was already committed,
executor rejection does not rewrite that terminal outcome; it closes the
callback registration and releases its payload/captures. Work-queue rejection
before work admission remains the existing synchronous/terminal `Shutdown`
path. There is no synchronous callback fallback.

When no executor is supplied, the runtime creates a private adapter whose
`post()` uses the existing `ModuleHub` task manager. This adapter is not a new
worker, queue, event bus, or run loop. Operation work and default callback
delivery therefore retain the previous event-hub scheduling behavior. An
injected executor changes only callback dispatch; operation admission,
cancellation, terminal first-winner rules, typed-result ownership, and
capacity accounting remain shared with the void lifecycle.

## Lifecycle and concurrency

Terminal state is committed under the operation mutex. The mutex is released
before calling external `post()`, which permits a reentrant executor to inspect
or submit work without a core-mutex deadlock. A callback task checks its
registration at execution time. Closing a queued registration suppresses it;
closing during an invocation lets that invocation finish. The callback guard
also handles an executor that accepts a task and then closes/drains it before
execution.

`await_callback_quiescence()` waits on the same condition variable that is
updated while holding the operation mutex. Every transition (delivery,
suppression, callback return, and dropped dispatch guard) notifies after the
predicate change, so there is no lost-wakeup window. A wait from the runtime's
own work or callback invocation remains a non-blocking self-observation, as in
ADR-0002. Executor callbacks must be short and must not synchronously reenter
`process_once()`.

The runtime owns the injected executor's shared lease but does not stop or
close it. Applications close or drain it according to its own contract. If it
drops accepted tasks, the guard suppresses those registrations; if it leaves
them queued, runtime shutdown may report `ShutdownTimedOut` until the executor
drains or drops them.

## Consequences

- deterministic fake executors can cover accepted, rejected, queued, running,
  closed, drained, and suppressed states without sleeps;
- the foundation target remains independent of `event-hub-cpp`;
- high-level APIs do not expose event-hub types through the executor seam;
- executor shutdown and callback quiescence are explicit application
  responsibilities rather than hidden blocking behavior;
- deadlines, futures, transport bridges, XRPC, kurlyk, and persistence remain
  outside this change.

