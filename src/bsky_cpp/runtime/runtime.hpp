/// \file runtime.hpp
/// \brief Event-hub-backed operation runtime for M1c.
#pragma once

#include "bsky_cpp/core/bsky_error.hpp"
#include "bsky_cpp/core/cancellation.hpp"
#include "bsky_cpp/core/result.hpp"

#include <event_hub/task.hpp>

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
                                MoveOnlyFunction>::value>::type>
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
    /// Maximum number of admitted, not-yet-terminal operations.
    std::size_t max_in_flight = 64U;
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

/// Event-hub-cpp-backed operation runtime.
class Runtime {
public:
    /// Monotonic clock type used for runtime deadlines.
    using TimePoint = std::chrono::steady_clock::time_point;

    /// Constructs a runtime with the selected execution options.
    /// \param options Admission and lifecycle options.
    explicit Runtime(RuntimeOptions options = {});
    /// Stops the runtime within its default bounded shutdown policy.
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    /// Move-constructs a runtime.
    Runtime(Runtime&&) noexcept = default;
    /// Move-assigns a runtime.
    /// \return This runtime after assignment.
    Runtime& operator=(Runtime&&) noexcept = default;

    /// Starts managed execution or initializes external execution.
    /// \return Success or a lifecycle error.
    Result<void> start();

    /// Processes one non-blocking external pass.
    /// \return Number of processed work items, or a lifecycle error.
    Result<std::size_t> process_once();

    /// Returns the earliest runtime deadline, if one is currently known.
    /// \return Earliest deadline, if any.
    std::optional<TimePoint> next_deadline();

    /// Admits one operation and queues it asynchronously.
    /// \param work Operation body.
    /// \param callback Optional terminal callback.
    /// \return An operation handle or a synchronous admission error.
    Result<OperationHandle> submit(OperationWork work,
                                   CompletionCallback callback = {});

    /// Requests shutdown and wakes active loops. This does not wait.
    void request_stop() noexcept;

    /// Stops admission and waits for operation/callback quiescence up to the
    /// supplied bound. A timeout returns `ErrorCategory::Shutdown` and leaves
    /// shared operation state alive for later cleanup.
    /// \param timeout Maximum shutdown wait.
    /// \return Success or a shutdown error.
    Result<void> shutdown(std::chrono::milliseconds timeout =
                              std::chrono::seconds(2));

    /// Returns whether new work can currently be admitted.
    /// \return `true` while admission is open.
    bool accepting() const noexcept;

private:
    std::shared_ptr<detail::RuntimeCore> m_core;
};

}  // namespace bsky_cpp::runtime
