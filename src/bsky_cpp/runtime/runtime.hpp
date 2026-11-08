/// \file runtime.hpp
/// \brief Event-hub-backed operation runtime for M1c.
#pragma once

#include "bsky_cpp/core/bsky_error.hpp"
#include "bsky_cpp/core/cancellation.hpp"
#include "bsky_cpp/core/result.hpp"
#include "bsky_cpp/transport/transport.hpp"

#include <event_hub/notifier.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace bsky_cpp::runtime {

namespace detail {
struct OperationState;
struct RuntimeCore;
struct RuntimeTestAccess;
}

/// A C++17 move-only callable used for operation work and callbacks.
template <typename Signature>
class MoveOnlyFunction;

template <typename Return, typename... Arguments>
/// Type-erased move-only callable specialization for the requested signature.
class MoveOnlyFunction<Return(Arguments...)> {
public:
    /// Constructs an empty callable.
    MoveOnlyFunction() noexcept = default;
    /// Constructs an empty callable from null.
    MoveOnlyFunction(std::nullptr_t) noexcept {}

    /// Constructs a callable by taking ownership of the supplied target.
    /// \tparam Callable Callable target type.
    /// \param callable Callable target.
    template <typename Callable,
              typename = typename std::enable_if<
                  !std::is_same<typename std::decay<Callable>::type,
                                MoveOnlyFunction>::value &&
                  std::is_invocable_r<Return, typename std::decay<Callable>::type&,
                                      Arguments...>::value>::type>
    MoveOnlyFunction(Callable&& callable)
        : m_target(std::make_unique<Model<typename std::decay<Callable>::type>>(
              std::forward<Callable>(callable))) {}

    /// Move-constructs a callable.
    MoveOnlyFunction(MoveOnlyFunction&&) noexcept = default;
    /// Move-assigns a callable.
    /// \return This callable after assignment.
    MoveOnlyFunction& operator=(MoveOnlyFunction&&) noexcept = default;
    MoveOnlyFunction(const MoveOnlyFunction&) = delete;
    MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;

    /// Returns whether a target is stored.
    explicit operator bool() const noexcept {
        return static_cast<bool>(m_target);
    }

    /// Clears the stored callable.
    void reset() noexcept {
        m_target.reset();
    }

    /// Invokes the stored target.
    /// \param arguments Arguments forwarded to the target.
    /// \return The target's return value.
    Return operator()(Arguments... arguments) {
        if (!m_target) throw std::bad_function_call();
        return m_target->invoke(std::forward<Arguments>(arguments)...);
    }

private:
    struct Concept {
        virtual ~Concept() = default;
        virtual Return invoke(Arguments&&... arguments) = 0;
    };

    template <typename Callable>
    struct Model final : Concept {
        explicit Model(Callable callable)
            : m_callable(std::move(callable)) {}

        Return invoke(Arguments&&... arguments) override {
            return m_callable(std::forward<Arguments>(arguments)...);
        }

        Callable m_callable;
    };

    std::unique_ptr<Concept> m_target;
};

/// Move-only task accepted by an `IExecutor`.
using ExecutorTask = MoveOnlyFunction<void()>;

/// Callback-dispatch boundary used by the operation runtime.
///
/// `post()` takes ownership of a move-only task. Returning `true` admits the
/// task; the executor must either invoke it once or destroy it. Destroying an
/// admitted task is observable by the runtime and suppresses that callback,
/// which keeps callback quiescence finite when an executor closes its queue.
/// Implementations must enqueue asynchronously and must not invoke the task
/// inline from `post()`. The runtime retains a shared ownership lease for the
/// executor until all dependent callback registrations are quiescent.
/// `post()` may be called concurrently by multiple runtime completion threads;
/// implementations must synchronize their admission state. An implementation
/// must not hold an internal queue lock while invoking or destroying a task:
/// task destruction may run user capture destructors and the dispatch guard may
/// reenter the runtime. The executor may be destroyed on the thread that runs
/// the final callback or drops the final accepted task, so its destructor must
/// support that lifetime and must not require a self-join.
class IExecutor {
public:
    virtual ~IExecutor() = default;

    /// Attempts to enqueue one move-only callback task.
    ///
    /// \param task Task owned by the executor after a successful admission.
    /// \return `true` when accepted; `false` when rejected before admission.
    /// \throws Any exception is treated by Runtime as rejection and must not
    ///         escape into user work or callback code.
    virtual bool post(ExecutorTask task) = 0;
};

/// Execution policy for the event-hub-backed runtime.
enum class RuntimeMode {
    Managed,  ///< Runtime owns the ModuleHub background loop.
    External  ///< Caller drives process_once() and owns the outer loop.
};

/// Runtime admission and lifecycle options.
struct RuntimeOptions {
    /// Execution mode.
    RuntimeMode mode = RuntimeMode::Managed;
    /// Maximum admitted operations through work and callback quiescence.
    /// Zero is invalid and makes start() fail with InvalidArgument.
    std::size_t max_in_flight = 64U;
    /// Optional external-loop notifier lease. notify() must be non-blocking
    /// and must not reenter the runtime. Ignored by managed execution.
    std::shared_ptr<event_hub::INotifier> notifier;
    /// Optional callback-dispatch executor. When empty, Runtime posts
    /// callbacks through the existing ModuleHub task manager; no second
    /// scheduler is created. The shared pointer is an explicit lifetime lease.
    std::shared_ptr<IExecutor> executor;
    /// Maximum number of admitted blocking-transport jobs, including queued
    /// and running work. Zero leaves the blocking bridge disabled and avoids
    /// creating its bounded private worker.
    std::size_t max_blocking_in_flight = 0U;
};

class OperationHandle;

/// Callback registration for one accepted operation.
class CallbackRegistration {
public:
    /// Constructs an empty registration.
    CallbackRegistration() noexcept = default;

    /// Closes registration. A queued callback is suppressed; an in-flight
    /// callback is allowed to finish.
    void close() noexcept;

    /// Returns whether this registration has been closed.
    /// \return `true` when closed.
    bool is_closed() const noexcept;

    /// Waits until queued delivery is suppressed or in-flight delivery ends.
    /// \param timeout Maximum wait duration.
    /// \return `true` when quiescent before the timeout.
    bool await_quiescence(std::chrono::milliseconds timeout) const;

private:
    explicit CallbackRegistration(std::shared_ptr<detail::OperationState> state) noexcept
        : m_state(std::move(state)) {}

    std::shared_ptr<detail::OperationState> m_state;
    friend class OperationHandle;
    friend class Runtime;
};

/// Observation/control handle for one accepted operation.
class OperationHandle {
public:
    /// Constructs an empty handle.
    OperationHandle() noexcept = default;

    /// Requests cancellation and wins the terminal race when completion has
    /// not already been recorded.
    void request_cancellation() noexcept;

    /// Returns whether one terminal outcome has been recorded.
    /// \return `true` when completion was recorded.
    bool is_completed() const noexcept;

    /// Observes terminal failure classification without copying the payload.
    /// \return Failure category, or no value before completion/on success.
    std::optional<ErrorCategory> error_category() const noexcept;

    /// Returns a separately retainable callback registration.
    /// \return Registration sharing this operation's dispatch state.
    CallbackRegistration callback_registration() const noexcept;

    /// Waits for terminal completion.
    /// \param timeout Maximum wait duration.
    /// \return `true` when completion was recorded before the timeout.
    bool wait_for_completion(std::chrono::milliseconds timeout) const;

    /// Closes the callback registration, if one was supplied at admission.
    void close_callback() noexcept;

    /// Waits for callback suppression or callback return.
    /// \param timeout Maximum wait duration.
    /// \return `true` when callback quiescence was reached before timeout.
    bool await_callback_quiescence(std::chrono::milliseconds timeout) const;

private:
    explicit OperationHandle(std::shared_ptr<detail::OperationState> state) noexcept
        : m_state(std::move(state)) {}

    std::shared_ptr<detail::OperationState> m_state;
    friend class Runtime;
    friend struct detail::RuntimeCore;
    friend struct detail::RuntimeTestAccess;
};

/// Non-blocking operation work accepted by the M1c runtime.
using OperationWork = MoveOnlyFunction<Result<void>(CancellationToken)>;

/// One-shot terminal callback. The result is delivered at most once.
using CompletionCallback = MoveOnlyFunction<void(Result<void>)>;

/// Typed operation work. The runtime keeps the value in the selected
/// callback channel; it is never copied into an operation handle.
template <typename T>
using TypedOperationWork = MoveOnlyFunction<Result<T>(CancellationToken)>;

/// One-shot typed terminal callback. The callback receives the move-only
/// `Result<T>` exactly once when a callback was supplied.
template <typename T>
using TypedCompletionCallback = MoveOnlyFunction<void(Result<T>)>;

/// Options that affect one admitted operation.
///
/// `deadline` is an absolute `std::chrono::steady_clock` time point. It is
/// scheduled on the runtime's existing event-hub task manager; it is not a
/// wall-clock time and it never creates a per-operation thread. An already
/// expired deadline admits a handle whose terminal outcome is `TimedOut`,
/// without starting work; an optional callback is still dispatched
/// asynchronously through the selected executor. A deadline is cooperative:
/// it requests cancellation at the scheduler dispatch point and never
/// forcibly interrupts a running work function. If timer registration is
/// rejected after admission, the operation completes with `Shutdown` through
/// the same terminal path.
struct OperationOptions {
    /// Absolute monotonic deadline, or no deadline.
    std::optional<std::chrono::steady_clock::time_point> deadline;
};

namespace detail {

/// Shared typed terminal payload slot. It owns no user work or callback.
template <typename T>
struct TypedResultSlot {
    /// Synchronizes producer completion with callback delivery. A running
    /// operation may finish its work after cancellation/timeout has already
    /// delivered the terminal callback.
    mutable std::mutex mutex;
    /// Terminal typed result retained until callback delivery.
    std::shared_ptr<Result<T>> result;

    void store(std::shared_ptr<Result<T>> value) noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        result = std::move(value);
    }

    std::shared_ptr<Result<T>> take() noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        return std::move(result);
    }

    void clear() noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        result.reset();
    }
};

}  // namespace detail

/// Event-hub-cpp-backed operation runtime for non-blocking void and typed work.
/// Work execution uses one managed hub thread or the external caller; callback
/// delivery uses the selected `IExecutor` (the default adapter uses that same
/// hub task manager).
/// submit(), stop, shutdown, and handles support concurrent calls. External
/// processing has one consumer; overlapping/reentrant passes return Conflict.
/// Object destruction/moving must not race access to that same Runtime object.
/// Work, callbacks, notifier calls, and capture destructors must be short.
/// Blocking transport work, when explicitly enabled, runs on one bounded
/// event-hub private worker and never on the hub loop. No future adapter is
/// implemented here. Operation deadlines use the pinned event-hub-cpp delayed-task API and
/// request cooperative cancellation; they do not forcibly interrupt running
/// user work. Managed timed awaiters rely on the pinned event-hub-cpp deadline
/// aggregation and registration wake-up correction.
class Runtime {
public:
    /// Monotonic clock type used for runtime deadlines.
    using TimePoint = std::chrono::steady_clock::time_point;

    /// Constructs a runtime with the selected execution options.
    /// \param options Admission and lifecycle options.
    explicit Runtime(RuntimeOptions options = {});
    /// Initiates stop without waiting; shared execution state owns cleanup.
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    /// Move-constructs a runtime.
    Runtime(Runtime&&) noexcept = default;
    /// Move-assigns a runtime.
    /// \param other Runtime whose executor lease is transferred.
    /// \return This runtime after assignment.
    Runtime& operator=(Runtime&& other) noexcept;

    /// Starts managed execution or initializes external execution.
    ///
    /// `start()` is single-admission and may race with `request_stop()` or
    /// `shutdown()`. If stop wins before startup completes, initialization is
    /// serialized with finalization, startup returns `Shutdown`, and no work is
    /// admitted. A stop after successful startup closes admission normally.
    /// \return Success or a lifecycle error.
    Result<void> start();

    /// Processes one non-blocking external pass.
    /// User work executes on the caller; callbacks execute on the selected
    /// executor and all user code must remain non-blocking. This is permitted
    /// only after a successful external `start()`; calls before readiness,
    /// during startup, or after stop return `Conflict`.
    /// \return Number of processed work items, or a lifecycle error.
    Result<std::size_t> process_once();

    /// Returns the earliest runtime deadline, if one is currently known.
    /// For external mode, ready work returns now; operation, task, and queued
    /// awaiter deadlines are aggregated. Call from the external consumer between
    /// passes after a successful `start()`; before readiness or after stop it
    /// returns no deadline.
    /// \return Earliest deadline, if any.
    std::optional<TimePoint> next_deadline();

    /// Admits one operation and queues it asynchronously.
    /// \param work Operation body.
    /// \param callback Optional terminal callback.
    /// \param options Optional absolute monotonic deadline. The trailing
    ///         parameter preserves the existing `(work, callback)` call shape.
    /// \return An operation handle or a synchronous admission error.
    Result<OperationHandle> submit(OperationWork work,
                                   CompletionCallback callback = {},
                                   OperationOptions options = {});

    /// Executes one synchronous Transport::send() call on the bounded
    /// blocking bridge lane and delivers its owned response asynchronously.
    ///
    /// The bridge uses one event-hub private worker for this Runtime. Admission
    /// is bounded by `RuntimeOptions::max_blocking_in_flight`; when that limit
    /// is zero or full, this method returns a synchronous error and invokes no
    /// callback. The transport is retained until work and callback quiescence.
    /// Cancellation and deadlines are cooperative: they commit the runtime
    /// outcome, request cancellation from Transport, and never forcibly
    /// interrupt a running `send()` call. A response that arrives after a
    /// losing cancellation or timeout cannot replace the terminal outcome.
    ///
    /// \param transport Shared lifetime lease for the blocking transport.
    /// \param request Owned request value.
    /// \param callback Optional typed response callback.
    /// \param options Optional absolute monotonic deadline.
    /// \return An operation handle or synchronous InvalidArgument,
    ///         Backpressure, or Shutdown error.
    Result<OperationHandle> submit_blocking(
        std::shared_ptr<Transport> transport,
        TransportRequest request,
        TypedCompletionCallback<TransportResponse> callback = {},
        OperationOptions options = {});

    /// Admits one typed operation and queues it asynchronously.
    ///
    /// The typed payload is moved into the callback channel. It is not
    /// available through `OperationHandle`, and no hidden copy is made.
    /// Work and callback captures have independent ownership: closing the
    /// callback releases its captures even while queued work remains, while
    /// a running callback retains its captures until it returns.
    /// `Result<T>` must be nothrow move constructible. This is a compile-time
    /// boundary: a throwing payload move cannot be recovered after the shared
    /// void lifecycle has committed terminal success.
    ///
    /// \tparam T Successful value type whose `Result<T>` move is `noexcept`.
    /// \param work Typed operation body.
    /// \param callback Optional typed terminal callback.
    /// \param options Optional absolute monotonic deadline. The trailing
    ///         parameter preserves the existing `(work, callback)` call shape.
    /// \return An operation handle or a synchronous admission error.
    template <typename T>
    Result<OperationHandle> submit_typed(
        TypedOperationWork<T> work,
        TypedCompletionCallback<T> callback = {},
        OperationOptions options = {}) {
        static_assert(!std::is_void<T>::value,
                      "Use Runtime::submit for Result<void> operations.");
        static_assert(
            std::is_nothrow_move_constructible<Result<T>>::value,
            "Runtime::submit_typed requires a nothrow-move-constructible Result<T>");
        if (!work) {
            return Result<OperationHandle>::failure(Error(
                ErrorCategory::InvalidArgument,
                "operation work is empty",
                "runtime.submit_typed"));
        }

        auto slot = std::make_shared<detail::TypedResultSlot<T>>();

        OperationWork erased_work = [work = std::move(work), slot](
                                        CancellationToken token) mutable {
            auto typed = work(token);
            auto produced = std::make_shared<Result<T>>(std::move(typed));
            const bool success = static_cast<bool>(*produced);
            std::optional<Error> error;
            if (!success) error.emplace(produced->error());
            slot->store(std::move(produced));
            if (success) {
                return Result<void>::success();
            }
            return Result<void>::failure(std::move(*error));
        };

        CompletionCallback erased_callback;
        if (callback) {
            erased_callback = [callback = std::move(callback), slot](
                                  Result<void> terminal) mutable {
                if (!terminal) {
                    slot->clear();
                    callback(Result<T>::failure(
                        Error(terminal.error())));
                    return;
                }

                auto produced = slot->take();
                if (produced) {
                    auto typed = std::move(*produced);
                    callback(std::move(typed));
                    return;
                }

                callback(Result<T>::failure(Error(
                    ErrorCategory::Internal,
                    "typed result was not produced",
                    "runtime.typed_operation")));
            };
        }

        return submit(std::move(erased_work), std::move(erased_callback),
                      std::move(options));
    }

    /// Closes admission, suppresses queued callbacks, and records Shutdown for
    /// unfinished operations. Running work/callbacks may finish; does not wait.
    void request_stop() noexcept;

    /// Stops admission and waits for work/callback quiescence and loop exit.
    /// Queued work is aborted and queued callbacks suppressed. No user callback
    /// is run by shutdown(). A timeout returns ShutdownTimedOut; a call from
    /// this runtime's work/callback returns QuiescencePending without self-wait.
    /// Shared state owns later cleanup in both cases.
    /// \param timeout Maximum shutdown wait.
    /// \return Success or a shutdown error.
    Result<void> shutdown(std::chrono::milliseconds timeout =
                              std::chrono::seconds(2));

    /// Returns whether new work can currently be admitted.
    /// \return `true` while admission is open.
    bool accepting() const noexcept;

private:
    std::shared_ptr<detail::RuntimeCore> m_core;
    friend struct detail::RuntimeTestAccess;
};

}  // namespace bsky_cpp::runtime
