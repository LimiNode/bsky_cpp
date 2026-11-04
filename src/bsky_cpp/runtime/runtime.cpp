#include "bsky_cpp/runtime/runtime.hpp"

#include <event_hub/module_hub.hpp>

#include <algorithm>
#include <exception>
#include <string>
#include <thread>
#include <vector>

namespace bsky_cpp::runtime {
namespace detail {

struct AdmissionCounter {
    std::atomic<std::size_t> value{0U};
};

struct RuntimeCore;

struct OperationState {
    mutable std::mutex mutex;
    std::condition_variable condition;
    CancellationSource cancellation;
    std::optional<Result<void>> terminal;
    CompletionCallback callback;
    std::shared_ptr<AdmissionCounter> admission;
    std::shared_ptr<RuntimeCore> core;
    bool callback_closed = false;
    bool callback_queued = false;
    bool callback_started = false;
};

class RuntimeModule final : public event_hub::Module {
public:
    explicit RuntimeModule(event_hub::EventBus& bus)
        : Module(bus) {}

    bool enqueue(event_hub::Task task) {
        return tasks().post(std::move(task)) != 0U;
    }

    std::shared_ptr<RuntimeCore> lifetime_lease;
};

struct RuntimeCore final : std::enable_shared_from_this<RuntimeCore> {
    explicit RuntimeCore(RuntimeOptions runtime_options)
        : options(runtime_options),
          admission(std::make_shared<AdmissionCounter>()) {
        if (options.max_in_flight == 0U) {
            options.max_in_flight = 1U;
        }
        module = &hub.emplace_module<RuntimeModule>();
    }

    ~RuntimeCore() {
        force_shutdown();
    }

    Result<void> start() {
        std::lock_guard<std::mutex> lock(lifecycle_mutex);
        if (started) {
            return Result<void>::failure(
                Error(ErrorCategory::Conflict, "runtime already started", "runtime.start"));
        }

        try {
            if (options.mode == RuntimeMode::Managed) {
                hub.start();
            } else {
                hub.initialize();
            }
            started = true;
            accepting.store(true, std::memory_order_release);
            return Result<void>::success();
        } catch (...) {
            return Result<void>::failure(
                Error(ErrorCategory::Internal, "runtime start failed", "runtime.start"));
        }
    }

    Result<std::size_t> process_once() {
        if (!is_external_running()) {
            return Result<std::size_t>::failure(
                Error(ErrorCategory::Conflict,
                      "runtime is not externally running",
                      "runtime.process_once"));
        }
        try {
            return Result<std::size_t>::success(hub.process_once());
        } catch (...) {
            return Result<std::size_t>::failure(
                Error(ErrorCategory::Internal,
                      "runtime processing failed",
                      "runtime.process_once"));
        }
    }

    std::optional<Runtime::TimePoint> next_deadline() {
        std::lock_guard<std::mutex> lock(lifecycle_mutex);
        if (!started) {
            return std::nullopt;
        }
        return hub.next_deadline();
    }

    Result<OperationHandle> submit(OperationWork work,
                                   CompletionCallback callback) {
        if (!work) {
            return Result<OperationHandle>::failure(
                Error(ErrorCategory::InvalidArgument,
                      "operation work is empty",
                      "runtime.submit"));
        }
        if (!accepting.load(std::memory_order_acquire)) {
            return Result<OperationHandle>::failure(
                Error(ErrorCategory::Shutdown,
                      "runtime is not accepting operations",
                      "runtime.submit"));
        }

        auto current = admission->value.load(std::memory_order_acquire);
        for (;;) {
            if (current >= options.max_in_flight) {
                return Result<OperationHandle>::failure(
                    Error(ErrorCategory::Backpressure,
                          "operation admission limit reached",
                          "runtime.submit"));
            }
            if (admission->value.compare_exchange_weak(
                    current,
                    current + 1U,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                break;
            }
        }

        // Shutdown may race the admission check above. Do not create a new
        // operation after admission has been closed.
        if (!accepting.load(std::memory_order_acquire)) {
            admission->value.fetch_sub(1U, std::memory_order_acq_rel);
            return Result<OperationHandle>::failure(
                Error(ErrorCategory::Shutdown,
                      "runtime is not accepting operations",
                      "runtime.submit"));
        }

        auto state = std::make_shared<OperationState>();
        state->admission = admission;
        state->core = shared_from_this();
        state->callback = std::move(callback);
        {
            std::lock_guard<std::mutex> lock(operations_mutex);
            operations.emplace_back(state);
        }

        const auto accepted = module->enqueue(event_hub::Task(
            [state, work = std::move(work)]() mutable {
                if (state->cancellation.token().is_cancellation_requested()) {
                    complete(state,
                             Result<void>::failure(Error(
                                 ErrorCategory::Cancelled,
                                 "operation cancelled",
                                 "runtime.operation")));
                    return;
                }

                Result<void> result;
                try {
                    result = work(state->cancellation.token());
                } catch (...) {
                    result = Result<void>::failure(Error(
                        ErrorCategory::Internal,
                        "operation work failed",
                        "runtime.operation"));
                }
                complete(state, std::move(result));
            }));

        if (!accepted) {
            admission->value.fetch_sub(1U, std::memory_order_acq_rel);
            std::lock_guard<std::mutex> lock(state->mutex);
            state->core.reset();
            return Result<OperationHandle>::failure(
                Error(ErrorCategory::Shutdown,
                      "runtime queue rejected operation",
                      "runtime.submit"));
        }

        return Result<OperationHandle>::success(OperationHandle(state));
    }

    void request_stop() noexcept {
        accepting.store(false, std::memory_order_release);
        try {
            hub.request_stop();
        } catch (...) {
        }
    }

    Result<void> shutdown(std::chrono::milliseconds timeout) {
        accepting.store(false, std::memory_order_release);
        cancel_operations();

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!all_quiescent()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                request_stop();
                return Result<void>::failure(Error(
                    ErrorCategory::Shutdown,
                    "runtime quiescence deadline expired",
                    "runtime.shutdown"));
            }

            if (options.mode == RuntimeMode::External) {
                (void)process_once();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        request_stop();
        std::lock_guard<std::mutex> lock(lifecycle_mutex);
        if (!started) {
            module->lifetime_lease.reset();
            return Result<void>::success();
        }
        try {
            if (options.mode == RuntimeMode::Managed) {
                hub.join();
            } else {
                hub.shutdown();
            }
            started = false;
            module->lifetime_lease.reset();
            return Result<void>::success();
        } catch (...) {
            return Result<void>::failure(Error(
                ErrorCategory::Shutdown,
                "runtime loop shutdown failed",
                "runtime.shutdown"));
        }
    }

    bool accepting_now() const noexcept {
        return accepting.load(std::memory_order_acquire);
    }

    bool is_external_running() const {
        std::lock_guard<std::mutex> lock(lifecycle_mutex);
        return started && options.mode == RuntimeMode::External;
    }

    static void complete(const std::shared_ptr<OperationState>& state,
                         Result<void> result) {
        bool schedule_callback = false;
        std::shared_ptr<RuntimeCore> core;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->terminal.has_value()) {
                return;
            }
            state->terminal.emplace(std::move(result));
            if (state->admission) {
                state->admission->value.fetch_sub(1U, std::memory_order_acq_rel);
                state->admission.reset();
            }
            if (state->callback && !state->callback_closed) {
                state->callback_queued = true;
                schedule_callback = true;
                core = state->core;
            }
            state->condition.notify_all();
        }

        if (!schedule_callback || !core) {
            return;
        }

        const auto accepted = core->module->enqueue(event_hub::Task(
            [state] { deliver_callback(state); }));
        if (!accepted) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->callback_queued = false;
            state->callback_closed = true;
            state->callback.reset();
            state->condition.notify_all();
        }
    }

    static void deliver_callback(const std::shared_ptr<OperationState>& state) {
        CompletionCallback callback;
        Result<void> result;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->callback_closed || !state->callback ||
                !state->terminal.has_value()) {
                state->callback_queued = false;
                state->callback.reset();
                state->condition.notify_all();
                return;
            }
            state->callback_started = true;
            state->callback_queued = false;
            callback = std::move(state->callback);
            result = *state->terminal;
        }

        try {
            callback(std::move(result));
        } catch (...) {
            // Callback exceptions cannot create a second terminal outcome.
        }

        std::lock_guard<std::mutex> lock(state->mutex);
        state->callback_started = false;
        state->condition.notify_all();
    }

    static bool is_quiescent(const std::shared_ptr<OperationState>& state) {
        std::lock_guard<std::mutex> lock(state->mutex);
        return state->terminal.has_value() && !state->callback_queued &&
               !state->callback_started;
    }

    void cancel_operations() {
        std::vector<std::shared_ptr<OperationState>> live;
        {
            std::lock_guard<std::mutex> lock(operations_mutex);
            for (const auto& weak : operations) {
                if (auto state = weak.lock()) {
                    live.push_back(std::move(state));
                }
            }
        }
        for (const auto& state : live) {
            state->cancellation.request_cancellation();
            complete(state,
                     Result<void>::failure(Error(ErrorCategory::Cancelled,
                                                  "operation cancelled",
                                                  "runtime.shutdown")));
        }
    }

    bool all_quiescent() {
        std::lock_guard<std::mutex> lock(operations_mutex);
        for (const auto& weak : operations) {
            if (auto state = weak.lock()) {
                if (!is_quiescent(state)) {
                    return false;
                }
            }
        }
        return true;
    }

    void force_shutdown() noexcept {
        try {
            accepting.store(false, std::memory_order_release);
            cancel_operations();
            hub.request_stop();
            std::lock_guard<std::mutex> lock(lifecycle_mutex);
            if (!started) {
                module->lifetime_lease.reset();
                return;
            }
            if (options.mode == RuntimeMode::Managed) {
                hub.join();
            } else {
                hub.shutdown();
            }
            started = false;
            module->lifetime_lease.reset();
        } catch (...) {
        }
    }

    RuntimeOptions options;
    event_hub::ModuleHub hub;
    RuntimeModule* module = nullptr;
    std::shared_ptr<AdmissionCounter> admission;
    std::atomic<bool> accepting{false};
    mutable std::mutex lifecycle_mutex;
    bool started = false;
    std::mutex operations_mutex;
    std::vector<std::weak_ptr<OperationState>> operations;
};

}  // namespace detail

void CallbackRegistration::close() noexcept {
    if (!m_state) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_state->mutex);
    m_state->callback_closed = true;
    if (!m_state->callback_started) {
        m_state->callback_queued = false;
        m_state->callback.reset();
    }
    m_state->condition.notify_all();
}

bool CallbackRegistration::is_closed() const noexcept {
    if (!m_state) {
        return true;
    }
    std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->callback_closed;
}

bool CallbackRegistration::await_quiescence(
    std::chrono::milliseconds timeout) const {
    if (!m_state) {
        return true;
    }
    std::unique_lock<std::mutex> lock(m_state->mutex);
    return m_state->condition.wait_for(lock, timeout, [state = m_state] {
        return !state->callback_queued && !state->callback_started;
    });
}

void OperationHandle::request_cancellation() noexcept {
    if (!m_state) {
        return;
    }
    m_state->cancellation.request_cancellation();
    detail::RuntimeCore::complete(
        m_state,
        Result<void>::failure(Error(ErrorCategory::Cancelled,
                                    "operation cancelled",
                                    "runtime.operation")));
}

bool OperationHandle::is_completed() const noexcept {
    if (!m_state) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->terminal.has_value();
}

bool OperationHandle::wait_for_completion(
    std::chrono::milliseconds timeout) const {
    if (!m_state) {
        return false;
    }
    std::unique_lock<std::mutex> lock(m_state->mutex);
    return m_state->condition.wait_for(lock, timeout, [state = m_state] {
        return state->terminal.has_value();
    });
}

void OperationHandle::close_callback() noexcept {
    CallbackRegistration(m_state).close();
}

bool OperationHandle::await_callback_quiescence(
    std::chrono::milliseconds timeout) const {
    return CallbackRegistration(m_state).await_quiescence(timeout);
}

Runtime::Runtime(RuntimeOptions options)
    : m_core(std::make_shared<detail::RuntimeCore>(options)) {
    m_core->module->lifetime_lease = m_core;
}

Runtime::~Runtime() {
    if (m_core) {
        (void)m_core->shutdown(std::chrono::seconds(2));
    }
}

Result<void> Runtime::start() {
    return m_core->start();
}

Result<std::size_t> Runtime::process_once() {
    return m_core->process_once();
}

std::optional<Runtime::TimePoint> Runtime::next_deadline() {
    return m_core->next_deadline();
}

Result<OperationHandle> Runtime::submit(OperationWork work,
                                         CompletionCallback callback) {
    return m_core->submit(std::move(work), std::move(callback));
}

void Runtime::request_stop() noexcept {
    if (m_core) {
        m_core->request_stop();
    }
}

Result<void> Runtime::shutdown(std::chrono::milliseconds timeout) {
    return m_core->shutdown(timeout);
}

bool Runtime::accepting() const noexcept {
    return m_core != nullptr && m_core->accepting_now();
}

}  // namespace bsky_cpp::runtime
