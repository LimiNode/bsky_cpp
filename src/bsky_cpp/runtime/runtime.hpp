/// \file runtime.hpp
/// \brief Event-hub-backed operation runtime for M1c.
#pragma once

#include "bsky_cpp/core/bsky_error.hpp"
#include "bsky_cpp/core/cancellation.hpp"
#include "bsky_cpp/core/result.hpp"

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
};

/// Non-blocking operation work accepted by the M1c runtime.
using OperationWork = MoveOnlyFunction<Result<void>(CancellationToken)>;

/// One-shot terminal callback. The result is delivered at most once.
using CompletionCallback = MoveOnlyFunction<void(Result<void>)>;

/// Event-hub-cpp-backed operation runtime for non-blocking void work.
/// Work/callback execution uses one managed hub thread or the external caller.
/// submit(), stop, shutdown, and handles support concurrent calls. External
/// processing has one consumer; overlapping/reentrant passes return Conflict.
/// Object destruction/moving must not race access to that same Runtime object.
/// Work, callbacks, notifier calls, and capture destructors must be short.
/// No network I/O, blocking-transport bridge, future adapter, or operation
/// deadlines are implemented here. Managed timed awaiters remain upstream-gated.
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
    /// \return Success or a lifecycle error.
    Result<void> start();

    /// Processes one non-blocking external pass.
    /// User work/callbacks execute on the caller; they must remain non-blocking.
    /// \return Number of processed work items, or a lifecycle error.
    Result<std::size_t> process_once();

    /// Returns the earliest runtime deadline, if one is currently known.
    /// For external mode, ready work returns now; task and queued awaiter
    /// deadlines are aggregated. Call from the external consumer between passes.
    /// \return Earliest deadline, if any.
    std::optional<TimePoint> next_deadline();

    /// Admits one operation and queues it asynchronously.
    /// \param work Operation body.
    /// \param callback Optional terminal callback.
    /// \return An operation handle or a synchronous admission error.
    Result<OperationHandle> submit(OperationWork work,
                                   CompletionCallback callback = {});

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
