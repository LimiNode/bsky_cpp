#include "bsky_cpp/runtime/runtime.hpp"
#include "bsky_cpp/runtime/detail/runtime_test_access.hpp"

#include <event_hub/module_hub.hpp>
#include <event_hub/task.hpp>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bsky_cpp::runtime {
namespace detail {

struct OperationState {
    std::shared_ptr<RuntimeCore> core;
    CancellationSource cancellation;
    OperationWork work;
    CompletionCallback callback;
    std::optional<Result<void>> payload;
    std::optional<ErrorCategory> error;
    bool completed = false;
    bool work_started = false;
    bool work_done = false;
    bool callback_closed = false;
    bool callback_started = false;
    bool callback_cleanup = false;
    bool callback_done = false;
    bool callback_dispatch_pending = false;
    bool stop_handled = false;
    bool blocking = false;
    event_hub::TaskId timeout_task_id = 0;
};

struct CallbackDispatchGuard {
    explicit CallbackDispatchGuard(std::weak_ptr<OperationState> value)
        : state(std::move(value)) {}

    ~CallbackDispatchGuard();
    void operator()() noexcept;

    std::weak_ptr<OperationState> state;
    std::atomic<bool> invoked{false};
};

// Identifies reentrant lifecycle/wait calls from work, callbacks, and captures.
thread_local RuntimeCore* executing_core = nullptr;

struct ExecutionScope {
    explicit ExecutionScope(RuntimeCore* core) : previous(executing_core) {
        executing_core = core;
    }
    ~ExecutionScope() { executing_core = previous; }
    RuntimeCore* previous;
};

class RuntimeModule final : public event_hub::Module {
public:
    RuntimeModule(event_hub::EventBus& bus, RuntimeCore& core)
        : Module(bus), m_core(core) {}
private:
    std::size_t on_process() override;
    RuntimeCore& m_core;
};

// Blocking transport work uses the upstream Module private-thread mode. The
// ModuleHub still owns lifecycle and scheduling; this lane only keeps a
// blocking send() off the hub's event-loop thread.
class BlockingTransportModule final : public event_hub::Module {
public:
    explicit BlockingTransportModule(event_hub::EventBus& bus)
        : Module(bus,
                 event_hub::ModuleOptions{
                     event_hub::ModuleExecutionMode::private_thread, 1U}) {}
};

class EventHubExecutor final : public IExecutor {
public:
    explicit EventHubExecutor(RuntimeCore& core) noexcept : m_core(core) {}

    bool post(MoveOnlyFunction<void()> task) override;

private:
    RuntimeCore& m_core;
};

struct RuntimeCore final : std::enable_shared_from_this<RuntimeCore> {
    explicit RuntimeCore(RuntimeOptions value) : options(std::move(value)) {
        module = &hub.emplace_module<RuntimeModule>(*this);
        if (options.max_blocking_in_flight != 0U) {
            blocking_module =
                &hub.emplace_module<BlockingTransportModule>();
        }
        executor = options.executor
                       ? std::move(options.executor)
                       : std::make_shared<EventHubExecutor>(*this);
    }

    Result<void> start() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (started || stopping || options.max_in_flight == 0U) {
                return Result<void>::failure(Error(
                    options.max_in_flight == 0U ? ErrorCategory::InvalidArgument
                                                : ErrorCategory::Conflict,
                    "runtime cannot start", "runtime.start"));
            }
            started = true;
            starting = true;
        }
        std::function<void()> startup_hook;
        std::function<void()> post_initialize_hook;
        try {
            {
                std::lock_guard<std::mutex> lock(mutex);
                startup_hook = startup_test_hook;
                post_initialize_hook = post_initialize_test_hook;
            }
            if (startup_hook) startup_hook();
            if (options.mode == RuntimeMode::External) {
                std::unique_lock<std::mutex> lifecycle(lifecycle_mutex);
                hub.set_notifier(options.notifier.get());
                hub.initialize();
                if (post_initialize_hook) post_initialize_hook();
                bool stop_after_start = false;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    starting = false;
                    stop_after_start = stopping;
                    if (!stop_after_start) ready = true;
                    condition.notify_all();
                }
                lifecycle.unlock();
                if (stop_after_start) {
                    finalize_external_with_consumer();
                    return Result<void>::failure(Error(
                        ErrorCategory::Shutdown, "runtime stopped during start", "runtime.start"));
                }
            } else {
                // The sole managed thread runs the upstream loop. Using run()
                // permits the last lease to end here without a self-join.
                std::thread([self = shared_from_this()] {
                    ExecutionScope scope(self.get());
                    try { self->hub.run(); } catch (...) { self->stop(); }
                    std::lock_guard<std::mutex> done(self->mutex);
                    self->exited = true;
                    self->condition.notify_all();
                }).detach();
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this] { return ready || exited; });
                starting = false;
                condition.notify_all();
            }
            bool stop_after_start = false;
            bool exited_after_start = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                stop_after_start = stopping;
                exited_after_start = exited;
                if (!exited && !stopping) accepting = true;
            }
            if (stop_after_start || exited_after_start) {
                if (options.mode == RuntimeMode::External) {
                    finalize_external_with_consumer();
                }
                return Result<void>::failure(Error(
                    ErrorCategory::Shutdown, "runtime stopped during start", "runtime.start"));
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (exited || stopping) {
                    return Result<void>::failure(Error(
                        ErrorCategory::Shutdown, "runtime stopped during start", "runtime.start"));
                }
            }
            return Result<void>::success();
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                starting = false;
                accepting = false;
                stopping = true;
                condition.notify_all();
            }
            if (options.mode == RuntimeMode::External) {
                finalize_external_with_consumer();
            } else {
                std::lock_guard<std::mutex> lock(mutex);
                exited = true;
                condition.notify_all();
            }
            return Result<void>::failure(Error(
                ErrorCategory::Internal, "runtime start failed", "runtime.start"));
        }
    }

    void loop_pass() {
        bool request_hub_stop = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            ready = true;
            condition.notify_all();
            request_hub_stop = stopping && operations.empty();
        }
        if (request_hub_stop) hub.request_stop();
    }

    Result<std::size_t> process_once() {
        if (executing_core == this) return Result<std::size_t>::failure(Error(
            ErrorCategory::Conflict, "reentrant external consumer", "runtime.process_once"));
        std::unique_lock<std::mutex> consumer(consumer_mutex, std::try_to_lock);
        if (!consumer.owns_lock()) {
            return Result<std::size_t>::failure(Error(
                ErrorCategory::Conflict, "external consumer already active", "runtime.process_once"));
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (options.mode != RuntimeMode::External || !ready || !accepting ||
                stopping || exited) {
                return Result<std::size_t>::failure(Error(
                    ErrorCategory::Conflict, "runtime is not externally running", "runtime.process_once"));
            }
            external_consumer_active = true;
        }
        ExecutionScope scope(this);
        try {
            const auto count = hub.process_once();
            finish_external_pass(consumer);
            return Result<std::size_t>::success(count);
        } catch (...) {
            stop();
            finish_external_pass(consumer);
            return Result<std::size_t>::failure(Error(
                ErrorCategory::Internal, "runtime processing failed", "runtime.process_once"));
        }
    }

    std::optional<Runtime::TimePoint> next_deadline() {
        if (executing_core == this) return std::nullopt;
        std::unique_lock<std::mutex> consumer(consumer_mutex, std::try_to_lock);
        if (!consumer.owns_lock()) return std::nullopt;
        std::optional<Runtime::TimePoint> deadline;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (options.mode != RuntimeMode::External || !ready || !accepting ||
                stopping || exited) return std::nullopt;
            external_consumer_active = true;
        }
        if (hub.has_pending()) {
            deadline = std::chrono::steady_clock::now();
        } else {
            deadline = hub.next_deadline();
            const auto awaiter =
                hub.bus().next_awaiter_deadline(event_hub::DispatchSource::queued);
            if (awaiter && (!deadline || *awaiter < *deadline)) deadline = awaiter;
        }
        release_external_consumer(consumer, false);
        return deadline;
    }

    Result<OperationHandle> submit(OperationWork work,
                                   CompletionCallback callback,
                                   OperationOptions operation_options,
                                   bool blocking = false) {
        if (!work) return Result<OperationHandle>::failure(Error(
            ErrorCategory::InvalidArgument, "operation work is empty", "runtime.submit"));

        auto state = std::make_shared<OperationState>();
        state->core = shared_from_this();
        state->work = std::move(work);
        state->callback = std::move(callback);
        state->callback_done = !state->callback;
        state->blocking = blocking;
        bool queued = false;
        bool expired = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!accepting) return Result<OperationHandle>::failure(Error(
                ErrorCategory::Shutdown, "runtime is not accepting operations", "runtime.submit"));
            if (operations.size() >= options.max_in_flight) {
                return Result<OperationHandle>::failure(Error(
                    ErrorCategory::Backpressure, "operation admission limit reached", "runtime.submit"));
            }
            if (blocking &&
                (blocking_module == nullptr ||
                 blocking_operations >= options.max_blocking_in_flight)) {
                return Result<OperationHandle>::failure(Error(
                    ErrorCategory::Backpressure,
                    "blocking transport admission limit reached",
                    "runtime.submit_blocking"));
            }
            // Register before leaving the admission boundary so stop observes
            // the operation while its upstream task is being submitted.
            operations.emplace(state.get(), state);
            if (blocking) ++blocking_operations;
            expired = operation_options.deadline &&
                      *operation_options.deadline <= std::chrono::steady_clock::now();
            if (submit_admission_hook) submit_admission_hook();
        }

        if (expired) {
            timeout(state);
            abandon_work(state);
            return Result<OperationHandle>::success(OperationHandle(state));
        }

        if (operation_options.deadline) {
            event_hub::TaskId timeout_id = 0;
            try {
                timeout_id = module->tasks().post_at(
                    *operation_options.deadline,
                    [weak = std::weak_ptr<OperationState>(state)] {
                        if (auto live = weak.lock()) live->core->timeout(live);
                    },
                    event_hub::TaskOptions{event_hub::TaskPriority::high});
            } catch (...) {
                timeout_id = 0;
            }

            if (timeout_id == 0) {
                try {
                    complete(state, Result<void>::failure(Error(
                        ErrorCategory::Shutdown,
                        "deadline registration was rejected",
                        "runtime.deadline")));
                } catch (...) {
                    emergency_complete(state, ErrorCategory::Shutdown);
                }
                abandon_work(state);
                return Result<OperationHandle>::success(OperationHandle(state));
            }

            bool cancel_timeout = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (state->completed) {
                    cancel_timeout = true;
                } else {
                    state->timeout_task_id = timeout_id;
                }
            }
            if (cancel_timeout) {
                (void)module->tasks().cancel(timeout_id);
                abandon_work(state);
                return Result<OperationHandle>::success(OperationHandle(state));
            }
        }

        try {
            auto* work_module =
                blocking ? static_cast<event_hub::Module*>(blocking_module)
                         : static_cast<event_hub::Module*>(module);
            queued = work_module != nullptr &&
                     work_module->tasks().post(
                [weak = std::weak_ptr<OperationState>(state)] {
                if (auto live = weak.lock()) live->core->execute(live);
                }) != 0U;
        } catch (...) {
            // Once registered, queue rejection is a terminal failure.
        }
        if (!queued) {
            try {
                if (consume_fault(RuntimeFaultPoint::SubmitRejectionError)) throw std::bad_alloc();
                complete(state, Result<void>::failure(Error(
                    ErrorCategory::Shutdown, "executor rejected admitted work", "runtime.dispatch")));
            } catch (...) {
                emergency_complete(state, ErrorCategory::Shutdown);
            }
            abandon_work(state);
        }
        return Result<OperationHandle>::success(OperationHandle(state));
    }

    Result<OperationHandle> submit_blocking(
        std::shared_ptr<Transport> transport,
        TransportRequest request,
        TypedCompletionCallback<TransportResponse> callback,
        OperationOptions operation_options) {
        if (!transport) {
            return Result<OperationHandle>::failure(Error(
                ErrorCategory::InvalidArgument,
                "blocking transport is empty",
                "runtime.submit_blocking"));
        }
        if (options.max_blocking_in_flight == 0U || blocking_module == nullptr) {
            return Result<OperationHandle>::failure(Error(
                ErrorCategory::InvalidArgument,
                "blocking transport bridge is disabled",
                "runtime.submit_blocking"));
        }

        static_assert(
            std::is_nothrow_move_constructible<Result<TransportResponse>>::value,
            "blocking transport requires a nothrow-move response result");
        auto slot = std::make_shared<detail::TypedResultSlot<TransportResponse>>();
        OperationWork work = [transport = std::move(transport),
                              request = std::move(request), slot](
                                 CancellationToken token) mutable {
            auto response = transport->send(request, token);
            slot->result = std::make_shared<Result<TransportResponse>>(
                std::move(response));
            if (*slot->result) return Result<void>::success();
            return Result<void>::failure(Error(slot->result->error()));
        };

        CompletionCallback erased_callback;
        if (callback) {
            erased_callback = [callback = std::move(callback), slot](
                                  Result<void> terminal) mutable {
                if (!terminal) {
                    slot->result.reset();
                    callback(Result<TransportResponse>::failure(
                        Error(terminal.error())));
                    return;
                }
                if (slot->result) {
                    auto response = std::move(*slot->result);
                    slot->result.reset();
                    callback(std::move(response));
                    return;
                }
                callback(Result<TransportResponse>::failure(Error(
                    ErrorCategory::Internal,
                    "blocking transport result was not produced",
                    "runtime.submit_blocking")));
            };
        }

        return submit(std::move(work), std::move(erased_callback),
                      std::move(operation_options), true);
    }

    void timeout(const std::shared_ptr<OperationState>& state) noexcept {
        // The cancellation request is part of the same first-winner critical
        // section as the terminal transition.  A timeout that loses to a
        // completion must not make an already successful operation appear
        // cancelled to a token retained by user work.
        try {
            complete(state,
                     Result<void>::failure(Error(
                         ErrorCategory::TimedOut,
                         "operation deadline expired",
                         "runtime.deadline")),
                     true);
        } catch (...) {
            // Error construction itself can allocate.  The timer callback is
            // noexcept at the TaskManager boundary, so fall back to the
            // allocation-free emergency terminal path instead of terminating
            // the hub loop.
            emergency_complete(state, ErrorCategory::TimedOut, true);
        }
    }

    void execute(const std::shared_ptr<OperationState>& state) {
        OperationWork work;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (state->work_started || state->work_done) return;
            state->work_started = true;
            work = std::move(state->work);
        }
        ExecutionScope scope(this);
        if (!state->cancellation.token().is_cancellation_requested()) {
            Result<void> outcome;
            bool outcome_ready = false;
            try {
                outcome = work(state->cancellation.token());
                outcome_ready = true;
            } catch (...) {
                try {
                    if (consume_fault(RuntimeFaultPoint::WorkError)) throw std::bad_alloc();
                    outcome = Result<void>::failure(Error(
                        ErrorCategory::Internal, "operation work threw", "runtime.operation"));
                    outcome_ready = true;
                } catch (...) {
                    emergency_complete(state, ErrorCategory::Internal);
                }
            }
            if (outcome_ready) complete(state, std::move(outcome));
        }
        work.reset();
        std::lock_guard<std::mutex> lock(mutex);
        state->work_done = true;
        retire(state);
    }

    void complete(const std::shared_ptr<OperationState>& state,
                  Result<void> result,
                  bool request_cancellation = false) {
        ErrorCategory emergency_category = result
                                               ? ErrorCategory::Internal
                                               : result.error().category();
        bool dispatch = false;
        event_hub::TaskId timeout_id = 0;
        try {
            if (consume_fault(RuntimeFaultPoint::Terminalization)) throw std::bad_alloc();
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (state->completed) return;
                if (request_cancellation) {
                    state->cancellation.request_cancellation();
                }
                timeout_id = state->timeout_task_id;
                state->timeout_task_id = 0;
                state->error = result ? std::nullopt
                                      : std::optional<ErrorCategory>(result.error().category());
                state->payload.emplace(std::move(result));
                state->completed = true;
                if (state->callback && !state->callback_closed) {
                    // Reserve delivery under the lifecycle mutex, but call
                    // user-provided executor code only after releasing it.
                    state->callback_dispatch_pending = true;
                    dispatch = true;
                } else {
                    state->payload.reset();
                    state->callback_done = true;
                }
                retire(state);
            }
        } catch (...) {
            emergency_complete(state, emergency_category, request_cancellation);
        }
        if (timeout_id != 0) (void)module->tasks().cancel(timeout_id);
        if (!dispatch) return;

        try {
            auto guard = std::make_shared<CallbackDispatchGuard>(
                std::weak_ptr<OperationState>(state));
            bool accepted = false;
            try {
                accepted = executor->post(MoveOnlyFunction<void()>(
                    [guard]() mutable { (*guard)(); }));
            } catch (...) {
                accepted = false;
            }
            if (!accepted) {
                if (consume_fault(RuntimeFaultPoint::DeliveryRejectionError)) {
                    throw std::bad_alloc();
                }
                callback_dispatch_abandoned(state);
            }
        } catch (...) {
            callback_dispatch_abandoned(state);
        }
    }

    void callback_dispatch_abandoned(const std::shared_ptr<OperationState>& state) noexcept {
        close_callback(state);
    }

    void deliver(const std::shared_ptr<OperationState>& state) {
        CompletionCallback callback;
        std::optional<Result<void>> result;
        bool missing_payload = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (state->callback_closed || state->callback_done || state->callback_started) return;
            state->callback_dispatch_pending = false;
            state->callback_started = true;
            callback = std::move(state->callback);
            if (state->payload) {
                result.emplace(std::move(*state->payload));
                state->payload.reset();
            } else {
                state->callback_started = false;
                state->callback_done = true;
                retire(state);
                missing_payload = true;
            }
        }
        if (missing_payload) {
            return;
        }
        ExecutionScope scope(this);
        try { callback(std::move(*result)); } catch (...) {}
        callback.reset(); // Captures must be destroyed before reporting quiescence.
        {
            std::lock_guard<std::mutex> lock(mutex);
            state->callback_started = false;
            state->callback_done = true;
            retire(state);
        }
    }

    void close_callback(const std::shared_ptr<OperationState>& state) {
        CompletionCallback callback;
        {
            std::lock_guard<std::mutex> lock(mutex);
            state->callback_closed = true;
            if (state->callback_started || state->callback_cleanup || state->callback_done) return;
            state->callback_dispatch_pending = false;
            state->callback_cleanup = true;
            callback = std::move(state->callback);
            state->payload.reset();
        }
        ExecutionScope scope(this);
        callback.reset();
        {
            std::lock_guard<std::mutex> lock(mutex);
            state->callback_cleanup = false;
            state->callback_done = true;
            retire(state);
        }
    }

    void abandon_work(const std::shared_ptr<OperationState>& state) {
        OperationWork work;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (state->work_started || state->work_done) return;
            state->work_started = true;
            work = std::move(state->work);
        }
        ExecutionScope scope(this);
        work.reset();
        std::lock_guard<std::mutex> lock(mutex);
        state->work_done = true;
        retire(state);
    }

    void cancel(const std::shared_ptr<OperationState>& state, ErrorCategory category) {
        state->cancellation.request_cancellation();
        try {
            complete(state, Result<void>::failure(Error(
                category, "operation stopped", "runtime.operation")));
        } catch (...) {
            emergency_complete(state, category);
        }
        // Keep capacity until the queued work thunk is consumed, so repeated
        // submit/cancel cannot turn the upstream queue into an unbounded one.
    }

    void emergency_complete(const std::shared_ptr<OperationState>& state,
                            ErrorCategory category,
                            bool request_cancellation = false) noexcept {
        CompletionCallback callback;
        event_hub::TaskId timeout_id = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (state->completed) return;
            if (request_cancellation) {
                state->cancellation.request_cancellation();
            }
            timeout_id = state->timeout_task_id;
            state->timeout_task_id = 0;
            state->callback_closed = true;
            state->callback_cleanup = true;
            state->error = category;
            state->payload.reset();
            state->completed = true;
            callback = std::move(state->callback);
            retire(state);
        }
        if (timeout_id != 0) (void)module->tasks().cancel(timeout_id);
        ExecutionScope scope(this);
        callback.reset();
        std::lock_guard<std::mutex> lock(mutex);
        state->callback_cleanup = false;
        state->callback_done = true;
        retire(state);
    }

    void stop() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex);
            accepting = false;
            stopping = true;
        }
        for (;;) {
            std::shared_ptr<OperationState> state;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (operations.empty()) break;
                for (const auto& item : operations) {
                    if (!item.second->stop_handled) {
                        item.second->stop_handled = true;
                        state = item.second;
                        break;
                    }
                }
            }
            if (!state) break;
            close_callback(state);
            cancel(state, ErrorCategory::Shutdown);
            abandon_work(state);
        }
        bool request_hub_stop = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            request_hub_stop = ready && operations.empty();
            condition.notify_all();
        }
        if (request_hub_stop) hub.request_stop();
        bool consumer_active = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            consumer_active = external_consumer_active;
        }
        if (options.mode == RuntimeMode::External && executing_core != this && !consumer_active) {
            std::unique_lock<std::mutex> consumer(consumer_mutex, std::try_to_lock);
            if (consumer.owns_lock()) try_finalize_external();
        }
    }

    Result<void> shutdown(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        stop();
        std::unique_lock<std::mutex> lock(mutex);
        if (executing_core == this) return pending(ErrorCategory::QuiescencePending,
                                                   "quiescence is pending on the current invocation");
        for (;;) {
            if (operations.empty() && (!started || exited)) return Result<void>::success();

            const bool external_finalize =
                options.mode == RuntimeMode::External && started && !exited &&
                !starting && !external_consumer_active && operations.empty();
            if (external_finalize) {
                lock.unlock();
                std::unique_lock<std::mutex> consumer(consumer_mutex);
                finalize_external();
                lock.lock();
                continue;
            }

            if (!condition.wait_until(lock, deadline, [this] {
                    return (operations.empty() && (!started || exited)) ||
                            (options.mode == RuntimeMode::External &&
                             !starting && operations.empty() && !external_consumer_active);
                })) {
                return pending(ErrorCategory::ShutdownTimedOut,
                               "shutdown deadline expired; quiescence is pending");
            }
        }
    }

    static Result<void> pending(ErrorCategory category, const char* message) {
        return Result<void>::failure(Error(category, message, "runtime.shutdown"));
    }

    void release_external_consumer(std::unique_lock<std::mutex>& consumer,
                                   bool observe_hook) {
        std::function<void()> hook;
        if (observe_hook) {
            std::lock_guard<std::mutex> lock(mutex);
            hook = external_pass_hook;
        }
        if (hook) hook();

        bool should_finalize = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            should_finalize = stopping && operations.empty();
            if (!should_finalize) {
                // Keep the core lock while handing the consumer mutex back.
                // stop() therefore cannot miss the handoff between its final
                // registry check and the end of this pass.
                external_consumer_active = false;
                consumer.unlock();
                condition.notify_all();
            }
        }
        if (should_finalize) finalize_external();
    }

    void finish_external_pass(std::unique_lock<std::mutex>& consumer) {
        release_external_consumer(consumer, true);
    }

    void try_finalize_external() noexcept {
        std::unique_lock<std::mutex> lifecycle(lifecycle_mutex, std::try_to_lock);
        if (!lifecycle.owns_lock()) return;
        finalize_external_locked();
    }

    void finalize_external_with_consumer() {
        // Teardown order is consumer -> lifecycle -> runtime state. Startup
        // releases lifecycle before taking this consumer lease.
        std::unique_lock<std::mutex> consumer(consumer_mutex);
        finalize_external();
    }

    // Called only by the external single consumer or a shutdown waiter.
    void finalize_external() {
        std::unique_lock<std::mutex> lifecycle(lifecycle_mutex);
        finalize_external_locked();
    }

    // lifecycle_mutex serializes this with external startup. No runtime mutex
    // is held while calling upstream lifecycle code.
    void finalize_external_locked() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (options.mode != RuntimeMode::External || !stopping || exited ||
                starting || !operations.empty()) return;
        }
        hub.shutdown();
        std::lock_guard<std::mutex> lock(mutex);
        exited = true;
        external_consumer_active = false;
        condition.notify_all();
    }

    // The mutex is held by every caller. Erasure releases capacity and keeps
    // the registry bounded even when observers retain completed handles.
    void retire(const std::shared_ptr<OperationState>& state) {
        bool request_hub_stop = false;
        if (state->completed && state->work_done && state->callback_done) {
            const auto erased = operations.erase(state.get());
            if (erased != 0U && state->blocking && blocking_operations != 0U) {
                --blocking_operations;
            }
            request_hub_stop = stopping && operations.empty() && ready;
        }
        condition.notify_all();
        if (request_hub_stop) hub.request_stop();
    }

    RuntimeOptions options;
    event_hub::ModuleHub hub;
    RuntimeModule* module = nullptr;
    BlockingTransportModule* blocking_module = nullptr;
    std::shared_ptr<IExecutor> executor;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::mutex lifecycle_mutex;
    std::mutex consumer_mutex;
    std::unordered_map<OperationState*, std::shared_ptr<OperationState>> operations;
    std::size_t blocking_operations = 0U;
    bool started = false;
    bool starting = false;
    bool ready = false;
    bool accepting = false;
    bool stopping = false;
    bool exited = false;
    bool external_consumer_active = false;
    std::function<void()> external_pass_hook;
    std::function<void()> startup_test_hook;
    std::function<void()> post_initialize_test_hook;
    // Test-only gate invoked after registration while the admission mutex is held.
    std::function<void()> submit_admission_hook;
    std::atomic<std::uint32_t> fault_mask{0U};

    bool consume_fault(RuntimeFaultPoint point) noexcept {
        const auto bit = std::uint32_t{1U} << static_cast<unsigned>(point);
        auto current = fault_mask.load(std::memory_order_acquire);
        while ((current & bit) != 0U) {
            if (fault_mask.compare_exchange_weak(current, current & ~bit,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {
                return true;
            }
        }
        return false;
    }
};

bool EventHubExecutor::post(MoveOnlyFunction<void()> task) {
    try {
        return m_core.module->tasks().post(
                   [task = std::move(task)]() mutable { task(); }) != 0U;
    } catch (...) {
        return false;
    }
}

CallbackDispatchGuard::~CallbackDispatchGuard() {
    if (invoked.load(std::memory_order_acquire)) return;
    if (auto live = state.lock()) live->core->callback_dispatch_abandoned(live);
}

void CallbackDispatchGuard::operator()() noexcept {
    if (invoked.exchange(true, std::memory_order_acq_rel)) return;
    if (auto live = state.lock()) live->core->deliver(live);
}

std::size_t RuntimeModule::on_process() { m_core.loop_pass(); return 0U; }

void RuntimeTestAccess::close_executor(Runtime& runtime) {
    runtime.m_core->module->tasks().close();
}

void RuntimeTestAccess::close_blocking_worker(Runtime& runtime) {
    if (runtime.m_core->blocking_module != nullptr) {
        runtime.m_core->blocking_module->tasks().close();
    }
}

void RuntimeTestAccess::set_admission_hook(Runtime& runtime,
                                            std::function<void()> hook) {
    std::lock_guard<std::mutex> lock(runtime.m_core->mutex);
    runtime.m_core->submit_admission_hook = std::move(hook);
}

void RuntimeTestAccess::set_startup_hook(Runtime& runtime,
                                         std::function<void()> hook) {
    std::lock_guard<std::mutex> lock(runtime.m_core->mutex);
    runtime.m_core->startup_test_hook = std::move(hook);
}

void RuntimeTestAccess::set_post_initialize_hook(Runtime& runtime,
                                                  std::function<void()> hook) {
    std::lock_guard<std::mutex> lock(runtime.m_core->mutex);
    runtime.m_core->post_initialize_test_hook = std::move(hook);
}

void RuntimeTestAccess::set_external_pass_hook(Runtime& runtime,
                                                std::function<void()> hook) {
    std::lock_guard<std::mutex> lock(runtime.m_core->mutex);
    runtime.m_core->external_pass_hook = std::move(hook);
}

void RuntimeTestAccess::inject_fault(Runtime& runtime, RuntimeFaultPoint point) {
    const auto bit = std::uint32_t{1U} << static_cast<unsigned>(point);
    runtime.m_core->fault_mask.fetch_or(bit, std::memory_order_release);
}

void RuntimeTestAccess::force_timeout(const OperationHandle& operation) noexcept {
    if (operation.m_state) operation.m_state->core->timeout(operation.m_state);
}

std::size_t RuntimeTestAccess::registered_operations(Runtime& runtime) {
    std::lock_guard<std::mutex> lock(runtime.m_core->mutex);
    return runtime.m_core->operations.size();
}

} // namespace detail

void CallbackRegistration::close() noexcept {
    if (m_state) m_state->core->close_callback(m_state);
}

bool CallbackRegistration::is_closed() const noexcept {
    if (!m_state) return true;
    std::lock_guard<std::mutex> lock(m_state->core->mutex);
    return m_state->callback_closed;
}

bool CallbackRegistration::await_quiescence(std::chrono::milliseconds timeout) const {
    if (!m_state) return true;
    auto core = m_state->core;
    std::unique_lock<std::mutex> lock(core->mutex);
    if (detail::executing_core == core.get()) return m_state->callback_done;
    return core->condition.wait_for(lock, timeout, [this] { return m_state->callback_done; });
}

void OperationHandle::request_cancellation() noexcept {
    if (m_state) m_state->core->cancel(m_state, ErrorCategory::Cancelled);
}

bool OperationHandle::is_completed() const noexcept {
    if (!m_state) return false;
    std::lock_guard<std::mutex> lock(m_state->core->mutex);
    return m_state->completed;
}

std::optional<ErrorCategory> OperationHandle::error_category() const noexcept {
    if (!m_state) return std::nullopt;
    std::lock_guard<std::mutex> lock(m_state->core->mutex);
    return m_state->error;
}

bool OperationHandle::wait_for_completion(std::chrono::milliseconds timeout) const {
    if (!m_state) return false;
    auto core = m_state->core;
    std::unique_lock<std::mutex> lock(core->mutex);
    if (detail::executing_core == core.get()) return m_state->completed;
    return core->condition.wait_for(lock, timeout, [this] { return m_state->completed; });
}

CallbackRegistration OperationHandle::callback_registration() const noexcept {
    return CallbackRegistration(m_state);
}

void OperationHandle::close_callback() noexcept { callback_registration().close(); }

bool OperationHandle::await_callback_quiescence(std::chrono::milliseconds timeout) const {
    return callback_registration().await_quiescence(timeout);
}

Runtime::Runtime(RuntimeOptions options)
    : m_core(std::make_shared<detail::RuntimeCore>(std::move(options))) {}

Runtime::~Runtime() { request_stop(); }

Runtime& Runtime::operator=(Runtime&& other) noexcept {
    if (this != &other) { request_stop(); m_core = std::move(other.m_core); }
    return *this;
}

Result<void> Runtime::start() {
    return m_core ? m_core->start()
                  : detail::RuntimeCore::pending(ErrorCategory::Shutdown,
                                                 "empty runtime");
}

Result<std::size_t> Runtime::process_once() {
    auto core = m_core;
    return core ? core->process_once() : Result<std::size_t>::failure(Error(
        ErrorCategory::Shutdown, "empty runtime", "runtime.process_once"));
}

std::optional<Runtime::TimePoint> Runtime::next_deadline() {
    return m_core ? m_core->next_deadline() : std::nullopt;
}

Result<OperationHandle> Runtime::submit(OperationWork work,
                                        CompletionCallback callback,
                                        OperationOptions options) {
    auto core = m_core;
    return core ? core->submit(std::move(work), std::move(callback),
                               std::move(options))
                : Result<OperationHandle>::failure(Error(
                      ErrorCategory::Shutdown, "empty runtime", "runtime.submit"));
}

Result<OperationHandle> Runtime::submit_blocking(
    std::shared_ptr<Transport> transport,
    TransportRequest request,
    TypedCompletionCallback<TransportResponse> callback,
    OperationOptions options) {
    auto core = m_core;
    return core ? core->submit_blocking(std::move(transport),
                                        std::move(request),
                                        std::move(callback),
                                        std::move(options))
                : Result<OperationHandle>::failure(Error(
                      ErrorCategory::Shutdown,
                      "empty runtime",
                      "runtime.submit_blocking"));
}

void Runtime::request_stop() noexcept {
    auto core = m_core;
    if (core) core->stop();
}

Result<void> Runtime::shutdown(std::chrono::milliseconds timeout) {
    auto core = m_core;
    return core ? core->shutdown(timeout) : Result<void>::success();
}

bool Runtime::accepting() const noexcept {
    if (!m_core) return false;
    std::lock_guard<std::mutex> lock(m_core->mutex);
    return m_core->accepting;
}

} // namespace bsky_cpp::runtime
