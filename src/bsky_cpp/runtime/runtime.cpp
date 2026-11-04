#include "bsky_cpp/runtime/runtime.hpp"
#include "bsky_cpp/runtime/detail/runtime_test_access.hpp"

#include <event_hub/module_hub.hpp>

#include <condition_variable>
#include <mutex>
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

struct RuntimeCore final : std::enable_shared_from_this<RuntimeCore> {
    explicit RuntimeCore(RuntimeOptions value) : options(std::move(value)) {
        module = &hub.emplace_module<RuntimeModule>(*this);
    }

    Result<void> start() {
        std::unique_lock<std::mutex> lock(mutex);
        if (started || stopping || options.max_in_flight == 0U) {
            return Result<void>::failure(Error(
                options.max_in_flight == 0U ? ErrorCategory::InvalidArgument
                                            : ErrorCategory::Conflict,
                "runtime cannot start", "runtime.start"));
        }
        started = true;
        try {
            if (options.mode == RuntimeMode::External) {
                hub.set_notifier(options.notifier.get());
                hub.initialize();
                ready = true;
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
                condition.wait(lock, [this] { return ready || exited; });
            }
            if (exited || stopping) {
                return Result<void>::failure(Error(
                    ErrorCategory::Shutdown, "runtime stopped during start", "runtime.start"));
            }
            accepting = true;
            return Result<void>::success();
        } catch (...) {
            stopping = true;
            exited = true;
            return Result<void>::failure(Error(
                ErrorCategory::Internal, "runtime start failed", "runtime.start"));
        }
    }

    void loop_pass() {
        std::lock_guard<std::mutex> lock(mutex);
        ready = true;
        condition.notify_all();
        if (stopping && operations.empty()) hub.request_stop();
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
            if (options.mode != RuntimeMode::External || !ready || exited) {
                return Result<std::size_t>::failure(Error(
                    ErrorCategory::Conflict, "runtime is not externally running", "runtime.process_once"));
            }
        }
        ExecutionScope scope(this);
        try {
            const auto count = hub.process_once();
            finalize_external();
            return Result<std::size_t>::success(count);
        } catch (...) {
            stop();
            finalize_external();
            return Result<std::size_t>::failure(Error(
                ErrorCategory::Internal, "runtime processing failed", "runtime.process_once"));
        }
    }

    std::optional<Runtime::TimePoint> next_deadline() {
        if (executing_core == this) return std::nullopt;
        std::unique_lock<std::mutex> consumer(consumer_mutex, std::try_to_lock);
        if (!consumer.owns_lock()) return std::nullopt;
        std::lock_guard<std::mutex> lock(mutex);
        if (options.mode != RuntimeMode::External || !ready || exited) return std::nullopt;
        if (hub.has_pending()) return std::chrono::steady_clock::now();
        auto deadline = hub.next_deadline();
        const auto awaiter = hub.bus().next_awaiter_deadline(event_hub::DispatchSource::queued);
        if (awaiter && (!deadline || *awaiter < *deadline)) deadline = awaiter;
        return deadline;
    }

    Result<OperationHandle> submit(OperationWork work, CompletionCallback callback) {
        if (!work) return Result<OperationHandle>::failure(Error(
            ErrorCategory::InvalidArgument, "operation work is empty", "runtime.submit"));

        auto state = std::make_shared<OperationState>();
        state->core = shared_from_this();
        state->work = std::move(work);
        state->callback = std::move(callback);
        state->callback_done = !state->callback;
        bool queued = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!accepting) return Result<OperationHandle>::failure(Error(
                ErrorCategory::Shutdown, "runtime is not accepting operations", "runtime.submit"));
            if (operations.size() >= options.max_in_flight) {
                return Result<OperationHandle>::failure(Error(
                    ErrorCategory::Backpressure, "operation admission limit reached", "runtime.submit"));
            }
            // Registration and queue admission share the shutdown boundary.
            operations.emplace(state.get(), state);
            try {
                queued = module->tasks().post([weak = std::weak_ptr<OperationState>(state)] {
                    if (auto live = weak.lock()) live->core->execute(live);
                }) != 0U;
            } catch (...) {
                // Once registered, queue rejection is a terminal failure.
            }
        }
        if (!queued) {
            complete(state, Result<void>::failure(Error(
                ErrorCategory::Shutdown, "executor rejected admitted work", "runtime.dispatch")));
            abandon_work(state);
        }
        return Result<OperationHandle>::success(OperationHandle(state));
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
            try { complete(state, work(state->cancellation.token())); }
            catch (...) {
                complete(state, Result<void>::failure(Error(
                    ErrorCategory::Internal, "operation work threw", "runtime.operation")));
            }
        }
        work.reset();
        std::lock_guard<std::mutex> lock(mutex);
        state->work_done = true;
        retire(state);
    }

    void complete(const std::shared_ptr<OperationState>& state, Result<void> result) {
        bool rejected = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (state->completed) return;
            if (state->callback && !state->callback_closed) {
                bool queued = false;
                try {
                    queued = module->tasks().post([weak = std::weak_ptr<OperationState>(state)] {
                        if (auto live = weak.lock()) live->core->deliver(live);
                    }) != 0U;
                } catch (...) {}
                if (!queued) {
                    result = Result<void>::failure(Error(
                        ErrorCategory::Shutdown, "executor rejected terminal delivery", "runtime.dispatch"));
                    rejected = true;
                }
            }
            state->error = result ? std::nullopt : std::optional<ErrorCategory>(result.error().category());
            state->payload.emplace(std::move(result));
            state->completed = true;
            retire(state);
        }
        if (rejected) close_callback(state);
    }

    void deliver(const std::shared_ptr<OperationState>& state) {
        CompletionCallback callback;
        std::optional<Result<void>> result;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (state->callback_closed || state->callback_done || state->callback_started) return;
            state->callback_started = true;
            callback = std::move(state->callback);
            result.emplace(std::move(*state->payload));
            state->payload.reset();
        }
        ExecutionScope scope(this);
        try { callback(std::move(*result)); } catch (...) {}
        callback.reset(); // Captures must be destroyed before reporting quiescence.
        std::lock_guard<std::mutex> lock(mutex);
        state->callback_started = false;
        state->callback_done = true;
        retire(state);
    }

    void close_callback(const std::shared_ptr<OperationState>& state) {
        CompletionCallback callback;
        {
            std::lock_guard<std::mutex> lock(mutex);
            state->callback_closed = true;
            if (state->callback_started || state->callback_cleanup || state->callback_done) return;
            state->callback_cleanup = true;
            callback = std::move(state->callback);
        }
        ExecutionScope scope(this);
        callback.reset();
        std::lock_guard<std::mutex> lock(mutex);
        state->callback_cleanup = false;
        state->callback_done = true;
        retire(state);
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
        complete(state, Result<void>::failure(Error(
            category, "operation stopped", "runtime.operation")));
        // Keep capacity until the queued work thunk is consumed, so repeated
        // submit/cancel cannot turn the upstream queue into an unbounded one.
    }

    void stop() {
        std::vector<std::shared_ptr<OperationState>> live;
        {
            std::lock_guard<std::mutex> lock(mutex);
            accepting = false;
            stopping = true;
            for (const auto& item : operations) live.push_back(item.second);
        }
        for (const auto& state : live) {
            close_callback(state);
            cancel(state, ErrorCategory::Shutdown);
            abandon_work(state);
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (ready && operations.empty()) hub.request_stop();
            condition.notify_all();
        }
        if (options.mode == RuntimeMode::External && executing_core != this) {
            std::unique_lock<std::mutex> consumer(consumer_mutex, std::try_to_lock);
            if (consumer.owns_lock()) finalize_external();
        }
    }

    Result<void> shutdown(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        stop();
        std::unique_lock<std::mutex> lock(mutex);
        if (executing_core == this) return pending("quiescence is pending on the current invocation");
        if (!condition.wait_until(lock, deadline, [this] {
                return operations.empty() && (!started || exited);
            })) return pending("shutdown deadline expired; quiescence is pending");
        return Result<void>::success();
    }

    static Result<void> pending(const char* message) {
        return Result<void>::failure(Error(ErrorCategory::Shutdown, message, "runtime.shutdown"));
    }

    // Called only by the external single consumer, never by a producer.
    void finalize_external() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!stopping || exited || !operations.empty()) return;
        }
        hub.shutdown();
        std::lock_guard<std::mutex> lock(mutex);
        exited = true;
        condition.notify_all();
    }

    // The mutex is held by every caller. Erasure releases capacity and keeps
    // the registry bounded even when observers retain completed handles.
    void retire(const std::shared_ptr<OperationState>& state) {
        if (state->completed && state->work_done && state->callback_done) operations.erase(state.get());
        condition.notify_all();
    }

    RuntimeOptions options;
    event_hub::ModuleHub hub;
    RuntimeModule* module = nullptr;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::mutex consumer_mutex;
    std::unordered_map<OperationState*, std::shared_ptr<OperationState>> operations;
    bool started = false;
    bool ready = false;
    bool accepting = false;
    bool stopping = false;
    bool exited = false;
};

std::size_t RuntimeModule::on_process() { m_core.loop_pass(); return 0U; }

void RuntimeTestAccess::close_executor(Runtime& runtime) {
    runtime.m_core->module->tasks().close();
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
    return m_core ? m_core->start() : detail::RuntimeCore::pending("empty runtime");
}

Result<std::size_t> Runtime::process_once() {
    auto core = m_core;
    return core ? core->process_once() : Result<std::size_t>::failure(Error(
        ErrorCategory::Shutdown, "empty runtime", "runtime.process_once"));
}

std::optional<Runtime::TimePoint> Runtime::next_deadline() {
    return m_core ? m_core->next_deadline() : std::nullopt;
}

Result<OperationHandle> Runtime::submit(OperationWork work, CompletionCallback callback) {
    auto core = m_core;
    return core ? core->submit(std::move(work), std::move(callback))
                : Result<OperationHandle>::failure(Error(
                      ErrorCategory::Shutdown, "empty runtime", "runtime.submit"));
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
