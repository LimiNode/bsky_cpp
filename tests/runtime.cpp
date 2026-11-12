#include "bsky_cpp/runtime/runtime.hpp"
#include "bsky_cpp/runtime/async_transport.hpp"
#include "bsky_cpp/runtime/detail/runtime_test_access.hpp"
#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// All failures, including failures on worker threads, must fail CTest.
#define BSKY_RUNTIME_CHECK(expression) do { \
    if (!bsky_cpp_test::check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)) \
        std::abort(); \
} while (false)

namespace {
using namespace std::chrono_literals;
using namespace bsky_cpp;
using namespace bsky_cpp::runtime;
using Access = bsky_cpp::runtime::detail::RuntimeTestAccess;
using Fault = bsky_cpp::runtime::detail::RuntimeFaultPoint;

class Gate {
public:
    void signal() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_open = true;
        m_condition.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> lock(m_mutex);
        BSKY_RUNTIME_CHECK(m_condition.wait_for(lock, 3s, [this] { return m_open; }));
    }
private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_open = false;
};

class ControlledExecutor final : public IExecutor {
public:
    enum class State { Accept, Reject, Queued, Running, Closed, Drain, Suppress };

    bool post(MoveOnlyFunction<void()> task) override {
        bool rejected = false;
        bool suppressed = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            rejected = m_state == State::Reject || m_state == State::Closed;
            suppressed = m_state == State::Suppress;
            if (!rejected && !suppressed) m_tasks.emplace_back(std::move(task));
        }
        // User captures and the runtime guard are never destroyed while the
        // fake's queue mutex is held.
        if (rejected || suppressed) task.reset();
        return !rejected;
    }

    void set_state(State state) {
        std::vector<MoveOnlyFunction<void()>> dropped;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = state;
            if (state == State::Closed || state == State::Suppress) {
                dropped.swap(m_tasks);
            }
        }
    }

    bool run_one() {
        MoveOnlyFunction<void()> task;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_tasks.empty()) return false;
            m_state = State::Running;
            task = std::move(m_tasks.front());
            m_tasks.erase(m_tasks.begin());
        }
        task();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_state == State::Running) m_state = State::Queued;
        }
        return true;
    }

    void drain() {
        set_state(State::Drain);
        while (run_one()) {}
        set_state(State::Accept);
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_tasks.size();
    }

private:
    mutable std::mutex m_mutex;
    State m_state = State::Accept;
    std::vector<MoveOnlyFunction<void()>> m_tasks;
};

bool run_one_when_ready(ControlledExecutor& executor,
                        std::chrono::milliseconds timeout = 1s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!executor.run_one()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}

struct ThrowingTypedPayload {
    explicit ThrowingTypedPayload(int input) : value(input) {}
    ThrowingTypedPayload(const ThrowingTypedPayload&) = delete;
    ThrowingTypedPayload& operator=(const ThrowingTypedPayload&) = delete;
    ThrowingTypedPayload(ThrowingTypedPayload&& other) {
        if (throw_on_move) throw std::runtime_error("intentional typed move failure");
        value = other.value;
    }
    ThrowingTypedPayload& operator=(ThrowingTypedPayload&& other) {
        if (throw_on_move) throw std::runtime_error("intentional typed move failure");
        value = other.value;
        return *this;
    }

    static bool throw_on_move;
    int value;
};

bool ThrowingTypedPayload::throw_on_move = false;

static_assert(std::is_nothrow_move_constructible<Result<int>>::value,
              "nothrow typed payloads must remain supported");
static_assert(!std::is_nothrow_move_constructible<Result<ThrowingTypedPayload>>::value,
              "throwing typed payloads must be rejected at compile time");

OperationWork success() {
    return [](CancellationToken) { return Result<void>::success(); };
}

TransportRequest blocking_request() {
    auto request = TransportRequest::create(
        HttpMethod::Get, "https://pds.example/xrpc/com.example.test");
    BSKY_RUNTIME_CHECK(request);
    return std::move(request.value());
}

Runtime blocking_external(std::size_t overall_limit,
                           std::size_t blocking_limit,
                           std::shared_ptr<IExecutor> executor = {}) {
    Runtime runtime({RuntimeMode::External,
                     overall_limit,
                     {},
                     std::move(executor),
                     blocking_limit});
    BSKY_RUNTIME_CHECK(runtime.start());
    return runtime;
}

class ThrowingTransport final : public Transport {
public:
    Result<TransportResponse> send(const TransportRequest&,
                                   CancellationToken) override {
        throw std::runtime_error("adapter secret must stay contained");
    }
};

Runtime external(std::size_t limit = 2U,
                 std::shared_ptr<IExecutor> executor = {}) {
    Runtime runtime({RuntimeMode::External, limit, {}, std::move(executor)});
    BSKY_RUNTIME_CHECK(runtime.start());
    return runtime;
}

void admission_capacity_and_terminal_races() {
    auto runtime = external(1U);
    int calls = 0;
    auto invalid = runtime.submit({}, [&](Result<void>) { ++calls; });
    BSKY_RUNTIME_CHECK(!invalid && invalid.error().category() == ErrorCategory::InvalidArgument);
    auto first = runtime.submit(success(), [&](Result<void> result) {
        BSKY_RUNTIME_CHECK(!result && result.error().category() == ErrorCategory::Cancelled);
        ++calls;
    });
    BSKY_RUNTIME_CHECK(first);
    auto handle = first.value();
    BSKY_RUNTIME_CHECK(!handle.await_callback_quiescence(0ms));
    for (int n = 0; n != 4; ++n) handle.request_cancellation();
    BSKY_RUNTIME_CHECK(handle.error_category() == ErrorCategory::Cancelled);
    auto full = runtime.submit(success(), [&](Result<void>) { ++calls; });
    BSKY_RUNTIME_CHECK(!full && full.error().category() == ErrorCategory::Backpressure);
    BSKY_RUNTIME_CHECK(calls == 0);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(handle.await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(calls == 1 && Access::registered_operations(runtime) == 0U);

    // Work completion wins even when cancellation precedes callback dispatch.
    auto completed = runtime.submit(success(), [&](Result<void> result) {
        BSKY_RUNTIME_CHECK(result);
        ++calls;
    });
    BSKY_RUNTIME_CHECK(completed);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    completed.value().request_cancellation();
    BSKY_RUNTIME_CHECK(completed.value().is_completed());
    BSKY_RUNTIME_CHECK(!completed.value().error_category());
    auto callback_pending = runtime.submit(success());
    BSKY_RUNTIME_CHECK(!callback_pending);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(calls == 2 && Access::registered_operations(runtime) == 0U);
    BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    BSKY_RUNTIME_CHECK(runtime.shutdown(0ms));
    BSKY_RUNTIME_CHECK(!runtime.submit(success()));
    BSKY_RUNTIME_CHECK(std::string(error_category_name(ErrorCategory::Backpressure)) == "backpressure");
}

void close_and_move_only_captures() {
    auto runtime = external(1U);
    for (bool close_after_terminal : {false, true}) {
        auto capture = std::make_shared<int>(42);
        std::weak_ptr<int> weak = capture;
        int calls = 0;
        auto result = runtime.submit(success(),
            [owned = std::make_unique<int>(7), capture = std::move(capture), &calls](Result<void>) {
                BSKY_RUNTIME_CHECK(*owned == 7 && *capture == 42);
                ++calls;
            });
        BSKY_RUNTIME_CHECK(result);
        if (close_after_terminal) BSKY_RUNTIME_CHECK(runtime.process_once());
        auto registration = result.value().callback_registration();
        registration.close();
        BSKY_RUNTIME_CHECK(registration.is_closed());
        BSKY_RUNTIME_CHECK(registration.await_quiescence(0ms) && weak.expired());
        BSKY_RUNTIME_CHECK(runtime.process_once());
        BSKY_RUNTIME_CHECK(calls == 0 && Access::registered_operations(runtime) == 0U);
    }
    int calls = 0;
    {
        auto result = runtime.submit(
            [owned = std::make_unique<int>(3)](CancellationToken) {
                BSKY_RUNTIME_CHECK(*owned == 3);
                throw 3;
                return Result<void>::success();
            },
            [owned = std::make_unique<int>(9), &calls](Result<void> result) {
                BSKY_RUNTIME_CHECK(*owned == 9);
                BSKY_RUNTIME_CHECK(!result && result.error().category() == ErrorCategory::Internal);
                ++calls;
                throw 9;
            });
        BSKY_RUNTIME_CHECK(result);
        // Dropping a handle neither cancels nor closes its callback.
    }
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(calls == 1 && Access::registered_operations(runtime) == 0U);
    BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
}

void queue_rejection_is_terminal() {
    for (bool reject_work : {false, true}) {
        auto runtime = external();
        int calls = 0;
        if (reject_work) {
            Access::close_executor(runtime);
            Access::inject_fault(runtime, Fault::SubmitRejectionError);
        }
        auto result = runtime.submit(
            [&runtime](CancellationToken) {
                Access::close_executor(runtime);
                return Result<void>::success();
            }, [&](Result<void>) { ++calls; });
        BSKY_RUNTIME_CHECK(result); // Admission committed before executor post.
        if (!reject_work) BSKY_RUNTIME_CHECK(runtime.process_once());
        BSKY_RUNTIME_CHECK(result.value().is_completed());
        if (reject_work) {
            BSKY_RUNTIME_CHECK(result.value().error_category() == ErrorCategory::Shutdown);
        } else {
            BSKY_RUNTIME_CHECK(!result.value().error_category());
        }
        BSKY_RUNTIME_CHECK(result.value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(calls == 0 && Access::registered_operations(runtime) == 0U);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }
}

void external_stop_without_loop() {
    auto runtime = external();
    int calls = 0;
    auto result = runtime.submit(success(), [&](Result<void>) { ++calls; });
    BSKY_RUNTIME_CHECK(result);
    runtime.request_stop();
    BSKY_RUNTIME_CHECK(runtime.shutdown(0ms));
    BSKY_RUNTIME_CHECK(result.value().is_completed());
    BSKY_RUNTIME_CHECK(result.value().error_category() == ErrorCategory::Shutdown);
    BSKY_RUNTIME_CHECK(result.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(calls == 0 && Access::registered_operations(runtime) == 0U);
}

void startup_stop_lifecycle() {
    // A stop before start makes startup a synchronous Conflict and remains
    // safely repeatable through shutdown.
    {
        Runtime runtime({RuntimeMode::External, 2U, {}});
        runtime.request_stop();
        auto start = runtime.start();
        BSKY_RUNTIME_CHECK(!start && start.error().category() == ErrorCategory::Conflict);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // The startup gate pauses after started=true and before hub.initialize().
    // A concurrent stop must defer finalization until initialization reaches
    // its safe lifecycle boundary.
    {
        Runtime runtime({RuntimeMode::External, 2U, {}});
        Gate startup_entered, release_start;
        Access::set_startup_hook(runtime, [&] {
            startup_entered.signal();
            release_start.wait();
        });
        Result<void> start_result = Result<void>::failure(
            Error(ErrorCategory::Internal, "start did not run"));
        Result<void> shutdown_result = Result<void>::failure(
            Error(ErrorCategory::Internal, "shutdown did not run"));
        std::thread starter([&] { start_result = runtime.start(); });
        startup_entered.wait();
        std::thread stopper([&] { shutdown_result = runtime.shutdown(1s); });
        runtime.request_stop();
        release_start.signal();
        starter.join();
        stopper.join();
        Access::set_startup_hook(runtime, {});
        BSKY_RUNTIME_CHECK(!start_result &&
                           start_result.error().category() == ErrorCategory::Shutdown);
        BSKY_RUNTIME_CHECK(shutdown_result);
        BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 0U);
    }

    // After upstream initialization, startup still owns the lifecycle gate
    // until readiness is published. A pending stop must keep the external
    // consumer closed throughout that handoff.
    {
        Runtime runtime({RuntimeMode::External, 2U, {}});
        Gate initialized, release_start;
        Access::set_post_initialize_hook(runtime, [&] {
            initialized.signal();
            release_start.wait();
        });
        Result<void> start_result = Result<void>::failure(
            Error(ErrorCategory::Internal, "post-init start did not run"));
        std::thread starter([&] { start_result = runtime.start(); });
        initialized.wait();
        runtime.request_stop();

        std::optional<Result<std::size_t>> process_result;
        std::optional<Runtime::TimePoint> deadline;
        std::thread processor([&] { process_result.emplace(runtime.process_once()); });
        std::thread deadline_reader([&] { deadline = runtime.next_deadline(); });
        processor.join();
        deadline_reader.join();
        BSKY_RUNTIME_CHECK(process_result.has_value() && !*process_result &&
                           process_result->error().category() == ErrorCategory::Conflict);
        BSKY_RUNTIME_CHECK(!deadline.has_value());

        release_start.signal();
        starter.join();
        Access::set_post_initialize_hook(runtime, {});
        BSKY_RUNTIME_CHECK(!start_result &&
                           start_result.error().category() == ErrorCategory::Shutdown);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
        BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 0U);
    }

    // Managed startup uses the same stop-before-loop handoff. The loop must
    // observe the stop request after its initialization and exit once.
    {
        Runtime runtime({RuntimeMode::Managed, 2U, {}});
        Gate startup_entered, release_start;
        Access::set_startup_hook(runtime, [&] {
            startup_entered.signal();
            release_start.wait();
        });
        Result<void> start_result = Result<void>::failure(
            Error(ErrorCategory::Internal, "managed start did not run"));
        std::thread starter([&] { start_result = runtime.start(); });
        startup_entered.wait();
        runtime.request_stop();
        release_start.signal();
        starter.join();
        Access::set_startup_hook(runtime, {});
        BSKY_RUNTIME_CHECK(!start_result &&
                           start_result.error().category() == ErrorCategory::Shutdown);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // Repeated concurrent shutdown remains idempotent after startup.
    {
        auto runtime = external();
        Result<void> first = Result<void>::failure(
            Error(ErrorCategory::Internal, "first shutdown did not run"));
        Result<void> second = Result<void>::failure(
            Error(ErrorCategory::Internal, "second shutdown did not run"));
        std::thread one([&] { first = runtime.shutdown(1s); });
        std::thread two([&] { second = runtime.shutdown(1s); });
        one.join();
        two.join();
        BSKY_RUNTIME_CHECK(first && second);
    }
}

void terminal_allocation_failure() {
    auto runtime = external(1U);
    int callbacks = 0;
    auto operation = runtime.submit(success(), [&](Result<void>) { ++callbacks; });
    BSKY_RUNTIME_CHECK(operation);
    Access::inject_fault(runtime, Fault::Terminalization);
    operation.value().request_cancellation();
    BSKY_RUNTIME_CHECK(operation.value().is_completed());
    BSKY_RUNTIME_CHECK(operation.value().error_category() == ErrorCategory::Cancelled);
    BSKY_RUNTIME_CHECK(operation.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(callbacks == 0 && Access::registered_operations(runtime) == 0U);
    BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
}

void execute_terminal_faults() {
    auto runtime = external(1U);
    int callbacks = 0;

    Access::inject_fault(runtime, Fault::Terminalization);
    auto success_fault = runtime.submit(success(), [&](Result<void>) { ++callbacks; });
    BSKY_RUNTIME_CHECK(success_fault);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(success_fault.value().is_completed());
    BSKY_RUNTIME_CHECK(success_fault.value().error_category() == ErrorCategory::Internal);
    BSKY_RUNTIME_CHECK(success_fault.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(callbacks == 0 && Access::registered_operations(runtime) == 0U);

    auto reusable = runtime.submit(success());
    BSKY_RUNTIME_CHECK(reusable);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(reusable.value().is_completed());
    BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 0U);

    Access::inject_fault(runtime, Fault::WorkError);
    auto throwing_fault = runtime.submit(
        [](CancellationToken) -> Result<void> { throw 7; },
        [&](Result<void>) { ++callbacks; });
    BSKY_RUNTIME_CHECK(throwing_fault);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(throwing_fault.value().is_completed());
    BSKY_RUNTIME_CHECK(throwing_fault.value().error_category() == ErrorCategory::Internal);
    BSKY_RUNTIME_CHECK(throwing_fault.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(callbacks == 0 && Access::registered_operations(runtime) == 0U);
    BSKY_RUNTIME_CHECK(runtime.shutdown(1s));

    auto rejection_runtime = external(1U);
    Access::inject_fault(rejection_runtime, Fault::DeliveryRejectionError);
    auto callback_rejection = rejection_runtime.submit(
        [&rejection_runtime](CancellationToken) {
            Access::close_executor(rejection_runtime);
            return Result<void>::success();
        }, [&](Result<void>) { ++callbacks; });
    BSKY_RUNTIME_CHECK(callback_rejection);
    BSKY_RUNTIME_CHECK(rejection_runtime.process_once());
    BSKY_RUNTIME_CHECK(callback_rejection.value().is_completed());
    BSKY_RUNTIME_CHECK(!callback_rejection.value().error_category());
    BSKY_RUNTIME_CHECK(callback_rejection.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(callbacks == 0 && Access::registered_operations(rejection_runtime) == 0U);
    BSKY_RUNTIME_CHECK(rejection_runtime.shutdown(1s));
}

void independent_runtimes_and_concurrent_stop() {
    auto first = external(1U);
    auto second = external(1U);
    Gate entered, release;
    auto blocked = first.submit([&](CancellationToken) {
        entered.signal();
        release.wait();
        return Result<void>::success();
    });
    BSKY_RUNTIME_CHECK(blocked);
    std::thread consumer([&] { BSKY_RUNTIME_CHECK(first.process_once()); });
    entered.wait();
    auto independent = second.submit(success());
    BSKY_RUNTIME_CHECK(independent);
    BSKY_RUNTIME_CHECK(second.process_once());
    BSKY_RUNTIME_CHECK(independent.value().is_completed());
    BSKY_RUNTIME_CHECK(second.shutdown(1s));
    std::thread stop_a([&] { first.request_stop(); });
    std::thread stop_b([&] { first.request_stop(); });
    stop_a.join();
    stop_b.join();
    auto pending = first.shutdown(0ms);
    BSKY_RUNTIME_CHECK(!pending &&
                       pending.error().category() == ErrorCategory::ShutdownTimedOut);
    release.signal();
    consumer.join();
    BSKY_RUNTIME_CHECK(first.shutdown(1s));
    BSKY_RUNTIME_CHECK(Access::registered_operations(first) == 0U);
    BSKY_RUNTIME_CHECK(Access::registered_operations(second) == 0U);
}

void external_finalization_handoff() {
    auto runtime = external(1U);
    Gate hook_entered, stop_done, release_hook, shutdown_returned;
    Access::set_external_pass_hook(runtime, [&] {
        BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 1U);
        hook_entered.signal();
        release_hook.wait();
    });
    std::atomic<int> callbacks{0};
    auto operation = runtime.submit(success(), [&](Result<void>) { ++callbacks; });
    BSKY_RUNTIME_CHECK(operation);
    std::atomic<bool> shutdown_ok{false};
    std::thread stopper([&] {
        hook_entered.wait();
        runtime.request_stop();
        stop_done.signal();
        shutdown_ok = static_cast<bool>(runtime.shutdown(1s));
        shutdown_returned.signal();
    });
    std::thread consumer([&] { BSKY_RUNTIME_CHECK(runtime.process_once()); });
    hook_entered.wait();
    stop_done.wait();
    release_hook.signal();
    shutdown_returned.wait();
    consumer.join();
    stopper.join();
    BSKY_RUNTIME_CHECK(shutdown_ok.load());
    BSKY_RUNTIME_CHECK(operation.value().is_completed());
    BSKY_RUNTIME_CHECK(!operation.value().error_category());
    BSKY_RUNTIME_CHECK(callbacks == 0);
    BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 0U);
}

void running_work_and_callback_quiescence(bool block_callback, RuntimeMode mode) {
    Runtime runtime({mode, 1U, {}});
    BSKY_RUNTIME_CHECK(runtime.start());
    Gate entered, release;
    auto capture = std::make_shared<int>(17);
    std::weak_ptr<int> weak = capture;
    auto result = runtime.submit(
        [&, capture](CancellationToken) {
            if (!block_callback) { entered.signal(); release.wait(); }
            BSKY_RUNTIME_CHECK(*capture == 17);
            return Result<void>::success();
        }, [&, capture](Result<void>) {
            if (block_callback) { entered.signal(); release.wait(); }
            BSKY_RUNTIME_CHECK(*capture == 17);
        });
    capture.reset();
    BSKY_RUNTIME_CHECK(result);
    std::thread consumer;
    if (mode == RuntimeMode::External) {
        if (block_callback) BSKY_RUNTIME_CHECK(runtime.process_once());
        consumer = std::thread([&] { BSKY_RUNTIME_CHECK(runtime.process_once()); });
    }
    entered.wait();
    BSKY_RUNTIME_CHECK(!runtime.process_once()); // One external consumer only.
    auto full = runtime.submit(success());
    BSKY_RUNTIME_CHECK(!full && full.error().category() == ErrorCategory::Backpressure);
    result.value().close_callback();
    if (block_callback) BSKY_RUNTIME_CHECK(!result.value().await_callback_quiescence(0ms));
    const auto before = std::chrono::steady_clock::now();
    auto pending = runtime.shutdown(10ms);
    BSKY_RUNTIME_CHECK(!pending &&
                       pending.error().category() == ErrorCategory::ShutdownTimedOut);
    BSKY_RUNTIME_CHECK(std::chrono::steady_clock::now() - before < 1s);
    BSKY_RUNTIME_CHECK(!weak.expired());
    release.signal();
    if (consumer.joinable()) consumer.join();
    BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    BSKY_RUNTIME_CHECK(weak.expired());
    BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 0U);
}

void concurrent_admission_cancel_stop() {
    for (int round = 0; round != 40; ++round) {
        auto runtime = external(64U);
        Gate start;
        Gate admissions_done;
        std::atomic<int> calls{0};
        auto initial = runtime.submit(success(), [&](Result<void>) { ++calls; });
        BSKY_RUNTIME_CHECK(initial);
        std::vector<OperationHandle> admitted;
        std::thread producer([&] {
            start.wait();
            for (int n = 0; n != 32; ++n) {
                auto result = runtime.submit(success(), [&](Result<void>) { ++calls; });
                if (result) admitted.push_back(result.value());
                else BSKY_RUNTIME_CHECK(result.error().category() == ErrorCategory::Shutdown);
            }
            admissions_done.signal();
        });
        std::thread canceller([&] {
            start.wait();
            initial.value().request_cancellation();
            initial.value().request_cancellation();
        });
        std::thread stopper([&] {
            start.wait();
            admissions_done.wait();
            BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
        });
        start.signal();
        producer.join(); canceller.join(); stopper.join();
        admitted.push_back(initial.value());
        for (auto& handle : admitted) {
            BSKY_RUNTIME_CHECK(handle.is_completed() && handle.await_callback_quiescence(0ms));
            BSKY_RUNTIME_CHECK(handle.error_category() == ErrorCategory::Shutdown ||
                               handle.error_category() == ErrorCategory::Cancelled);
        }
        BSKY_RUNTIME_CHECK(calls == 0 && !runtime.submit(success()));
        BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 0U);
    }
}

void racing_admission_cancel_stop() {
    // The private hook pauses a producer after it has crossed the admission
    // boundary while the core mutex is held. Stop announces its intent and
    // waits for that same boundary, making the race deterministic without
    // changing production ordering or using timing sleeps.
    {
        auto runtime = external(2U);
        Gate admitted_boundary, shutdown_requested, stop_returned;
        std::atomic<int> callbacks{0};
        Access::set_admission_hook(runtime, [&] {
            admitted_boundary.signal();
            shutdown_requested.wait();
        });

        std::unique_ptr<Result<OperationHandle>> admitted;
        std::thread producer([&] {
            admitted = std::make_unique<Result<OperationHandle>>(
                runtime.submit(success(), [&](Result<void>) { ++callbacks; }));
        });
        admitted_boundary.wait();

        std::thread stopper([&] {
            shutdown_requested.signal();
            runtime.request_stop();
            stop_returned.signal();
            BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
        });
        stop_returned.wait();
        producer.join();
        stopper.join();
        Access::set_admission_hook(runtime, {});

        BSKY_RUNTIME_CHECK(admitted && *admitted);
        BSKY_RUNTIME_CHECK(admitted->value().is_completed() &&
                           admitted->value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(admitted->value().error_category() == ErrorCategory::Shutdown);
        BSKY_RUNTIME_CHECK(callbacks == 0);
        BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 0U);
        auto rejected = runtime.submit(success(), [&](Result<void>) { ++callbacks; });
        BSKY_RUNTIME_CHECK(!rejected &&
                           rejected.error().category() == ErrorCategory::Shutdown);
    }

    // The opposite ordering must reject synchronously and invoke no callback.
    {
        auto runtime = external(2U);
        Gate stop_returned;
        std::atomic<int> callbacks{0};
        std::thread stopper([&] {
            BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
            stop_returned.signal();
        });
        stop_returned.wait();
        auto rejected = runtime.submit(success(), [&](Result<void>) { ++callbacks; });
        stopper.join();
        BSKY_RUNTIME_CHECK(!rejected &&
                           rejected.error().category() == ErrorCategory::Shutdown);
        BSKY_RUNTIME_CHECK(callbacks == 0);
        BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 0U);
    }
}

void self_shutdown_and_destruction(RuntimeMode mode) {
    auto runtime = std::make_unique<Runtime>(RuntimeOptions{mode, 2U, {}});
    BSKY_RUNTIME_CHECK(runtime->start());
    Gate allow_callback, callback_returned;
    auto capture = std::make_shared<int>(1);
    std::weak_ptr<int> weak = capture;
    OperationHandle handle;
    auto admitted = runtime->submit(success(), [&, capture = std::move(capture)](Result<void>) {
        allow_callback.wait();
        BSKY_RUNTIME_CHECK(!handle.await_callback_quiescence(1s));
        const auto before = std::chrono::steady_clock::now();
        auto pending = runtime->shutdown(1s);
        BSKY_RUNTIME_CHECK(!pending &&
                           pending.error().category() == ErrorCategory::QuiescencePending);
        BSKY_RUNTIME_CHECK(std::chrono::steady_clock::now() - before < 500ms);
        runtime.reset(); // Last public owner is released on its own executor.
        BSKY_RUNTIME_CHECK(*capture == 1);
        callback_returned.signal();
    });
    BSKY_RUNTIME_CHECK(admitted);
    handle = admitted.value();
    allow_callback.signal();
    if (mode == RuntimeMode::External) {
        BSKY_RUNTIME_CHECK(runtime->process_once());
        BSKY_RUNTIME_CHECK(runtime->process_once());
    }
    callback_returned.wait();
    BSKY_RUNTIME_CHECK(handle.await_callback_quiescence(1s));
    BSKY_RUNTIME_CHECK(weak.expired());
}

void notifier_and_move_assignment() {
    auto notifier = std::make_shared<event_hub::SyncNotifier>();
    Runtime runtime({RuntimeMode::External, 2U, notifier});
    BSKY_RUNTIME_CHECK(runtime.start());
    auto generation = notifier->generation();
    auto pending = runtime.submit(success());
    BSKY_RUNTIME_CHECK(pending && notifier->generation() != generation);
    BSKY_RUNTIME_CHECK(runtime.next_deadline().has_value());
    runtime = Runtime{}; // Old runtime must stop even with an observer lease.
    BSKY_RUNTIME_CHECK(pending.value().is_completed());
    BSKY_RUNTIME_CHECK(pending.value().error_category() == ErrorCategory::Shutdown);
    Runtime zero({RuntimeMode::Managed, 0U, {}});
    BSKY_RUNTIME_CHECK(!zero.start());
}

void typed_operations() {
    auto runtime = external(2U);
    std::atomic<int> callbacks{0};

    auto invalid = runtime.submit_typed<int>({}, {});
    BSKY_RUNTIME_CHECK(!invalid &&
                       invalid.error().category() == ErrorCategory::InvalidArgument);

    auto success_result = runtime.submit_typed<int>(
        [](CancellationToken) { return Result<int>::success(42); },
        [&callbacks](Result<int> result) {
            BSKY_RUNTIME_CHECK(result && result.value() == 42);
            ++callbacks;
        });
    BSKY_RUNTIME_CHECK(success_result);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(success_result.value().is_completed());
    BSKY_RUNTIME_CHECK(!success_result.value().error_category());
    BSKY_RUNTIME_CHECK(success_result.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(callbacks == 1);

    auto error_result = runtime.submit_typed<int>(
        [](CancellationToken) {
            return Result<int>::failure(Error(
                ErrorCategory::Protocol, "typed failure", "runtime.test"));
        },
        [&callbacks](Result<int> result) {
            BSKY_RUNTIME_CHECK(!result &&
                               result.error().category() == ErrorCategory::Protocol &&
                               result.error().message() == "typed failure");
            ++callbacks;
        });
    BSKY_RUNTIME_CHECK(error_result);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(error_result.value().error_category() ==
                       ErrorCategory::Protocol);
    BSKY_RUNTIME_CHECK(callbacks == 2);

    std::atomic<bool> work_ran{false};
    auto cancelled = runtime.submit_typed<int>(
        [&work_ran](CancellationToken) {
            work_ran = true;
            return Result<int>::success(9);
        },
        [&callbacks](Result<int> result) {
            BSKY_RUNTIME_CHECK(!result &&
                               result.error().category() == ErrorCategory::Cancelled);
            ++callbacks;
        });
    BSKY_RUNTIME_CHECK(cancelled);
    cancelled.value().request_cancellation();
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(!work_ran.load());
    BSKY_RUNTIME_CHECK(cancelled.value().error_category() == ErrorCategory::Cancelled);
    BSKY_RUNTIME_CHECK(callbacks == 3);

    std::atomic<bool> move_only_delivered{false};
    auto move_only = runtime.submit_typed<std::unique_ptr<int>>(
        [payload = std::make_unique<int>(7)](CancellationToken) mutable {
            return Result<std::unique_ptr<int>>::success(std::move(payload));
        },
        [capture = std::make_unique<int>(11), &move_only_delivered](
            Result<std::unique_ptr<int>> result) mutable {
            BSKY_RUNTIME_CHECK(result && result.value() &&
                               *result.value() == 7 && *capture == 11);
            move_only_delivered = true;
        });
    BSKY_RUNTIME_CHECK(move_only);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(move_only_delivered.load());

    auto throwing = runtime.submit_typed<int>(
        [](CancellationToken) -> Result<int> { throw 17; },
        [&callbacks](Result<int> result) {
            BSKY_RUNTIME_CHECK(!result &&
                               result.error().category() == ErrorCategory::Internal);
            ++callbacks;
        });
    BSKY_RUNTIME_CHECK(throwing);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(throwing.value().error_category() == ErrorCategory::Internal);
    BSKY_RUNTIME_CHECK(callbacks == 4);

    auto reusable = external(1U);
    auto capture = std::make_shared<int>(5);
    std::weak_ptr<int> weak_capture = capture;
    auto suppressed = reusable.submit_typed<int>(
        [capture](CancellationToken) { return Result<int>::success(*capture); },
        [capture](Result<int>) {});
    BSKY_RUNTIME_CHECK(suppressed);
    capture.reset();
    suppressed.value().close_callback();
    BSKY_RUNTIME_CHECK(reusable.process_once());
    BSKY_RUNTIME_CHECK(reusable.process_once());
    BSKY_RUNTIME_CHECK(weak_capture.expired());
    auto after_cleanup = reusable.submit_typed<int>(
        [](CancellationToken) { return Result<int>::success(1); });
    BSKY_RUNTIME_CHECK(after_cleanup);
    BSKY_RUNTIME_CHECK(reusable.process_once());
    BSKY_RUNTIME_CHECK(reusable.shutdown(1s));

    auto rejected_runtime = external(1U);
    Access::close_executor(rejected_runtime);
    auto rejected = rejected_runtime.submit_typed<int>(
        [](CancellationToken) { return Result<int>::success(1); },
        [](Result<int>) { BSKY_RUNTIME_CHECK(false); });
    BSKY_RUNTIME_CHECK(rejected);
    BSKY_RUNTIME_CHECK(rejected.value().is_completed());
    BSKY_RUNTIME_CHECK(rejected.value().error_category() == ErrorCategory::Shutdown);
    BSKY_RUNTIME_CHECK(rejected.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(rejected_runtime.shutdown(1s));

    BSKY_RUNTIME_CHECK(runtime.shutdown(1s));

    Runtime managed;
    BSKY_RUNTIME_CHECK(managed.start());
    std::atomic<bool> managed_delivered{false};
    auto managed_result = managed.submit_typed<int>(
        [](CancellationToken) { return Result<int>::success(13); },
        [&managed_delivered](Result<int> result) {
            managed_delivered = result && result.value() == 13;
        });
    BSKY_RUNTIME_CHECK(managed_result);
    BSKY_RUNTIME_CHECK(managed_result.value().wait_for_completion(1s));
    BSKY_RUNTIME_CHECK(managed_result.value().await_callback_quiescence(1s));
    BSKY_RUNTIME_CHECK(managed_delivered.load());
    BSKY_RUNTIME_CHECK(managed.shutdown(1s));
}

void typed_callback_lifetime() {
    auto runtime = external(1U);
    std::atomic<bool> work_ran{false};
    auto callback_marker = std::make_shared<int>(1);
    std::weak_ptr<int> callback_weak = callback_marker;
    auto queued = runtime.submit_typed<int>(
        [&work_ran](CancellationToken) {
            work_ran = true;
            return Result<int>::success(1);
        },
        [callback_marker](Result<int>) { BSKY_RUNTIME_CHECK(false); });
    BSKY_RUNTIME_CHECK(queued);
    callback_marker.reset();
    queued.value().close_callback();
    BSKY_RUNTIME_CHECK(queued.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(callback_weak.expired());
    BSKY_RUNTIME_CHECK(!work_ran.load());
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(work_ran.load());
    BSKY_RUNTIME_CHECK(queued.value().is_completed());
    BSKY_RUNTIME_CHECK(runtime.shutdown(1s));

    auto blocked_runtime = external(1U);
    Gate entered, release;
    auto blocked_marker = std::make_shared<int>(2);
    std::weak_ptr<int> blocked_weak = blocked_marker;
    auto blocked = blocked_runtime.submit_typed<int>(
        [&](CancellationToken) {
            entered.signal();
            release.wait();
            return Result<int>::success(2);
        },
        [blocked_marker](Result<int>) { BSKY_RUNTIME_CHECK(false); });
    BSKY_RUNTIME_CHECK(blocked);
    blocked_marker.reset();
    std::thread consumer([&] { BSKY_RUNTIME_CHECK(blocked_runtime.process_once()); });
    entered.wait();
    blocked.value().close_callback();
    BSKY_RUNTIME_CHECK(blocked.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(blocked_weak.expired());
    release.signal();
    consumer.join();
    BSKY_RUNTIME_CHECK(blocked.value().is_completed());
    BSKY_RUNTIME_CHECK(blocked_runtime.shutdown(1s));
}

void typed_terminal_races_and_faults() {
    auto runtime = external(2U);
    std::atomic<int> callbacks{0};

    auto committed = runtime.submit_typed<int>(
        [](CancellationToken) { return Result<int>::success(7); },
        [&callbacks](Result<int> result) {
            BSKY_RUNTIME_CHECK(result && result.value() == 7);
            ++callbacks;
        });
    BSKY_RUNTIME_CHECK(committed);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    committed.value().request_cancellation();
    BSKY_RUNTIME_CHECK(committed.value().error_category() == std::nullopt);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(callbacks == 1);

    Gate entered, release;
    std::atomic<int> cancelled_callbacks{0};
    auto running = runtime.submit_typed<int>(
        [&](CancellationToken) {
            entered.signal();
            release.wait();
            return Result<int>::success(8);
        },
        [&cancelled_callbacks](Result<int> result) {
            BSKY_RUNTIME_CHECK(!result &&
                               result.error().category() == ErrorCategory::Cancelled);
            ++cancelled_callbacks;
        });
    BSKY_RUNTIME_CHECK(running);
    std::thread consumer([&] { BSKY_RUNTIME_CHECK(runtime.process_once()); });
    entered.wait();
    running.value().request_cancellation();
    BSKY_RUNTIME_CHECK(running.value().is_completed());
    BSKY_RUNTIME_CHECK(running.value().error_category() == ErrorCategory::Cancelled);
    BSKY_RUNTIME_CHECK(!running.value().await_callback_quiescence(0ms));
    release.signal();
    consumer.join();
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(running.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(cancelled_callbacks == 1);

    auto fault_runtime = external(1U);
    std::atomic<bool> fault_callback{false};
    auto fault_marker = std::make_shared<int>(3);
    std::weak_ptr<int> fault_weak = fault_marker;
    Access::inject_fault(fault_runtime, Fault::Terminalization);
    auto faulted = fault_runtime.submit_typed<std::unique_ptr<int>>(
        [](CancellationToken) {
            return Result<std::unique_ptr<int>>::success(std::make_unique<int>(3));
        },
        [fault_marker, &fault_callback](Result<std::unique_ptr<int>>) {
            fault_callback = true;
        });
    BSKY_RUNTIME_CHECK(faulted);
    fault_marker.reset();
    BSKY_RUNTIME_CHECK(fault_runtime.process_once());
    BSKY_RUNTIME_CHECK(faulted.value().is_completed());
    BSKY_RUNTIME_CHECK(faulted.value().error_category() == ErrorCategory::Internal);
    BSKY_RUNTIME_CHECK(faulted.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(!fault_callback && fault_weak.expired());
    BSKY_RUNTIME_CHECK(Access::registered_operations(fault_runtime) == 0U);
    BSKY_RUNTIME_CHECK(fault_runtime.shutdown(1s));

    std::atomic<int> throwing_callbacks{0};
    auto throwing_callback = runtime.submit_typed<int>(
        [](CancellationToken) { return Result<int>::success(9); },
        [&throwing_callbacks](Result<int>) {
            ++throwing_callbacks;
            throw 9;
        });
    BSKY_RUNTIME_CHECK(throwing_callback);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(throwing_callbacks == 1);
    BSKY_RUNTIME_CHECK(throwing_callback.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 0U);
    BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
}

void injected_executor_lifecycle() {
    auto executor = std::make_shared<ControlledExecutor>();
    auto runtime = external(8U, executor);

    int callbacks = 0;
    auto accepted = runtime.submit(success(), [&](Result<void> result) {
        BSKY_RUNTIME_CHECK(result);
        ++callbacks;
    });
    BSKY_RUNTIME_CHECK(accepted);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(accepted.value().is_completed());
    BSKY_RUNTIME_CHECK(executor->size() == 1U && callbacks == 0);
    BSKY_RUNTIME_CHECK(executor->run_one());
    BSKY_RUNTIME_CHECK(callbacks == 1 && accepted.value().await_callback_quiescence(0ms));

    auto typed = runtime.submit_typed<std::unique_ptr<int>>(
        [](CancellationToken) {
            return Result<std::unique_ptr<int>>::success(std::make_unique<int>(23));
        },
        [&callbacks](Result<std::unique_ptr<int>> result) {
            BSKY_RUNTIME_CHECK(result && result.value() && *result.value() == 23);
            ++callbacks;
        });
    BSKY_RUNTIME_CHECK(typed);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(executor->run_one());
    BSKY_RUNTIME_CHECK(callbacks == 2);

    // Rejection happens after terminal commitment and therefore suppresses
    // delivery without rewriting a successful terminal outcome.
    executor->set_state(ControlledExecutor::State::Reject);
    auto rejected = runtime.submit(success(), [&](Result<void>) { ++callbacks; });
    BSKY_RUNTIME_CHECK(rejected);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(rejected.value().is_completed() &&
                       !rejected.value().error_category());
    BSKY_RUNTIME_CHECK(rejected.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(callbacks == 2);
    executor->set_state(ControlledExecutor::State::Accept);

    // An accepted queued task may be dropped by executor shutdown. Its guard
    // closes the registration, releases captures, and wakes shutdown.
    auto marker = std::make_shared<int>(7);
    std::weak_ptr<int> weak_marker = marker;
    auto closed = runtime.submit(success(), [marker, &callbacks](Result<void>) {
        ++callbacks;
    });
    BSKY_RUNTIME_CHECK(closed);
    marker.reset();
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(executor->size() == 1U);
    executor->set_state(ControlledExecutor::State::Closed);
    BSKY_RUNTIME_CHECK(closed.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(weak_marker.expired() && callbacks == 2);
    BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 0U);

    // Suppression before execution follows the same observed-drop path.
    executor->set_state(ControlledExecutor::State::Suppress);
    auto suppressed = runtime.submit(success(), [&](Result<void>) { ++callbacks; });
    BSKY_RUNTIME_CHECK(suppressed);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(suppressed.value().await_callback_quiescence(0ms));
    BSKY_RUNTIME_CHECK(callbacks == 2);
    executor->set_state(ControlledExecutor::State::Accept);

    // Cancellation wins before work executes; completion wins once work has
    // committed, even when callback delivery remains queued.
    auto cancelled = runtime.submit(success(), [&](Result<void> result) {
        BSKY_RUNTIME_CHECK(!result && result.error().category() == ErrorCategory::Cancelled);
        ++callbacks;
    });
    BSKY_RUNTIME_CHECK(cancelled);
    cancelled.value().request_cancellation();
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(executor->run_one());
    BSKY_RUNTIME_CHECK(cancelled.value().error_category() == ErrorCategory::Cancelled);

    auto completed = runtime.submit(success(), [&](Result<void> result) {
        BSKY_RUNTIME_CHECK(result);
        ++callbacks;
    });
    BSKY_RUNTIME_CHECK(completed);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    completed.value().request_cancellation();
    BSKY_RUNTIME_CHECK(!completed.value().error_category());
    BSKY_RUNTIME_CHECK(executor->run_one());

    // A callback can close its executor and submit reentrant work without
    // observing a core-mutex deadlock or an inline callback fallback.
    std::optional<OperationHandle> nested;
    auto reentrant = runtime.submit(success(), [&](Result<void>) {
        executor->set_state(ControlledExecutor::State::Closed);
        auto next = runtime.submit(success());
        BSKY_RUNTIME_CHECK(next);
        nested.emplace(next.value());
    });
    BSKY_RUNTIME_CHECK(reentrant);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(executor->run_one());
    BSKY_RUNTIME_CHECK(nested.has_value());
    BSKY_RUNTIME_CHECK(runtime.process_once());
    BSKY_RUNTIME_CHECK(nested->is_completed());

    // Drain and a throwing callback both leave the registration quiescent.
    executor->set_state(ControlledExecutor::State::Accept);
    auto throwing = runtime.submit(success(), [&](Result<void>) {
        ++callbacks;
        throw std::runtime_error("callback failure");
    });
    BSKY_RUNTIME_CHECK(throwing);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    executor->drain();
    BSKY_RUNTIME_CHECK(throwing.value().await_callback_quiescence(0ms));

    Gate callback_entered, release_callback;
    auto concurrent = runtime.submit(success(), [&](Result<void>) {
        callback_entered.signal();
        release_callback.wait();
        ++callbacks;
    });
    BSKY_RUNTIME_CHECK(concurrent);
    BSKY_RUNTIME_CHECK(runtime.process_once());
    std::thread callback_runner([&] { BSKY_RUNTIME_CHECK(executor->run_one()); });
    callback_entered.wait();
    Result<void> shutdown_result = Result<void>::failure(
        Error(ErrorCategory::Internal, "shutdown did not run"));
    std::thread stopper([&] { shutdown_result = runtime.shutdown(1s); });
    release_callback.signal();
    callback_runner.join();
    stopper.join();
    BSKY_RUNTIME_CHECK(shutdown_result);
    BSKY_RUNTIME_CHECK(concurrent.value().await_callback_quiescence(0ms));

    BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 0U);
    BSKY_RUNTIME_CHECK(callbacks == 6);
}

void operation_deadline_semantics() {
    // An expired deadline commits TimedOut at admission, never starts work,
    // and still uses the normal asynchronous callback channel.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto runtime = external(2U, executor);
        std::atomic<bool> work_ran{false};
        std::atomic<int> callbacks{0};
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() - 1ms;
        auto operation = runtime.submit(
            [&work_ran](CancellationToken) {
                work_ran = true;
                return Result<void>::success();
            },
            [&callbacks](Result<void> result) {
                BSKY_RUNTIME_CHECK(!result &&
                                   result.error().category() == ErrorCategory::TimedOut);
                ++callbacks;
            },
            options);
        BSKY_RUNTIME_CHECK(operation && operation.value().is_completed());
        BSKY_RUNTIME_CHECK(operation.value().error_category() == ErrorCategory::TimedOut);
        BSKY_RUNTIME_CHECK(!work_ran.load() && executor->size() == 1U);
        BSKY_RUNTIME_CHECK(executor->run_one());
        BSKY_RUNTIME_CHECK(callbacks == 1 &&
                           operation.value().await_callback_quiescence(0ms));
        operation.value().request_cancellation();
        BSKY_RUNTIME_CHECK(operation.value().error_category() == ErrorCategory::TimedOut);
        BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 0U);
        auto recycled = runtime.submit(success());
        BSKY_RUNTIME_CHECK(recycled && runtime.process_once());
        BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 0U);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // A real external scheduler pass still covers the integration path where
    // the delayed task is due before queued work is processed.  The state
    // machine races below use force_timeout() instead of scheduler timing.
    {
        auto runtime = external();
        std::atomic<bool> work_ran{false};
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() + 20ms;
        auto operation = runtime.submit(
            [&work_ran](CancellationToken) {
                work_ran = true;
                return Result<void>::success();
            }, {}, options);
        BSKY_RUNTIME_CHECK(operation);
        const auto deadline = options.deadline.value();
        BSKY_RUNTIME_CHECK(runtime.next_deadline().has_value());
        std::this_thread::sleep_until(deadline + 1ms);
        BSKY_RUNTIME_CHECK(runtime.process_once());
        BSKY_RUNTIME_CHECK(operation.value().error_category() == ErrorCategory::TimedOut);
        BSKY_RUNTIME_CHECK(!work_ran.load());
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // Cancellation wins before timeout.  The later attempted timeout cannot
    // rewrite the terminal state or create a second delivery.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto runtime = external(2U, executor);
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() + 1s;
        std::atomic<int> callbacks{0};
        auto operation = runtime.submit(
            success(),
            [&callbacks](Result<void> result) {
                BSKY_RUNTIME_CHECK(!result &&
                                   result.error().category() == ErrorCategory::Cancelled);
                ++callbacks;
            },
            options);
        BSKY_RUNTIME_CHECK(operation);
        operation.value().request_cancellation();
        BSKY_RUNTIME_CHECK(operation.value().error_category() == ErrorCategory::Cancelled);
        Access::force_timeout(operation.value());
        BSKY_RUNTIME_CHECK(runtime.process_once());
        BSKY_RUNTIME_CHECK(executor->run_one());
        BSKY_RUNTIME_CHECK(operation.value().error_category() == ErrorCategory::Cancelled);
        BSKY_RUNTIME_CHECK(callbacks == 1 &&
                           operation.value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // Completion wins before timeout.  force_timeout() invokes the same
    // production transition as the delayed task, but after completion the
    // retained token must remain uncancelled.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto runtime = external(2U, executor);
        auto observed_token = std::make_shared<CancellationToken>();
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() + 1s;
        std::atomic<int> callbacks{0};
        auto operation = runtime.submit(
            [observed_token](CancellationToken token) {
                *observed_token = token;
                return Result<void>::success();
            },
            [&callbacks](Result<void> result) {
                BSKY_RUNTIME_CHECK(result);
                ++callbacks;
            },
            options);
        BSKY_RUNTIME_CHECK(operation);
        BSKY_RUNTIME_CHECK(runtime.process_once());
        BSKY_RUNTIME_CHECK(operation.value().error_category() == std::nullopt);
        Access::force_timeout(operation.value());
        BSKY_RUNTIME_CHECK(executor->run_one());
        BSKY_RUNTIME_CHECK(!observed_token->is_cancellation_requested());
        BSKY_RUNTIME_CHECK(callbacks == 1 &&
                           operation.value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // A running user function is not preempted.  The timeout transition is
    // forced while the work is blocked, then the work is released and its
    // later success attempt loses the already committed TimedOut outcome.
    {
        auto runtime = external();
        Gate entered, release;
        std::atomic<bool> finished{false};
        auto observed_token = std::make_shared<CancellationToken>();
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() + 1s;
        auto operation = runtime.submit(
            [&](CancellationToken token) {
                *observed_token = token;
                entered.signal();
                release.wait();
                finished = true;
                return Result<void>::success();
            }, {}, options);
        BSKY_RUNTIME_CHECK(operation);
        std::thread consumer([&] { BSKY_RUNTIME_CHECK(runtime.process_once()); });
        entered.wait();
        Access::force_timeout(operation.value());
        BSKY_RUNTIME_CHECK(operation.value().error_category() == ErrorCategory::TimedOut);
        BSKY_RUNTIME_CHECK(observed_token->is_cancellation_requested());
        release.signal();
        consumer.join();
        BSKY_RUNTIME_CHECK(finished.load() &&
                           operation.value().error_category() == ErrorCategory::TimedOut);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // Typed operations use the same timeout outcome and callback executor.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto runtime = external(2U, executor);
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() - 1ms;
        std::atomic<int> callbacks{0};
        auto operation = runtime.submit_typed<std::unique_ptr<int>>(
            [](CancellationToken) {
                return Result<std::unique_ptr<int>>::success(std::make_unique<int>(3));
            },
            [&callbacks](Result<std::unique_ptr<int>> result) {
                BSKY_RUNTIME_CHECK(!result &&
                                   result.error().category() == ErrorCategory::TimedOut);
                ++callbacks;
            },
            options);
        BSKY_RUNTIME_CHECK(operation && operation.value().is_completed());
        BSKY_RUNTIME_CHECK(executor->run_one());
        BSKY_RUNTIME_CHECK(callbacks == 1 &&
                           operation.value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // Typed completion wins before timeout, while a timeout-first typed
    // operation delivers a typed TimedOut error and never delivers success.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto runtime = external(4U, executor);
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() + 1s;
        std::atomic<int> callbacks{0};
        auto completed = runtime.submit_typed<int>(
            [](CancellationToken) { return Result<int>::success(9); },
            [&callbacks](Result<int> result) {
                BSKY_RUNTIME_CHECK(result && result.value() == 9);
                ++callbacks;
            },
            options);
        BSKY_RUNTIME_CHECK(completed);
        BSKY_RUNTIME_CHECK(runtime.process_once());
        Access::force_timeout(completed.value());
        BSKY_RUNTIME_CHECK(executor->run_one());
        BSKY_RUNTIME_CHECK(completed.value().error_category() == std::nullopt && callbacks == 1);

        auto timed_out = runtime.submit_typed<int>(
            [](CancellationToken) { return Result<int>::success(10); },
            [&callbacks](Result<int> result) {
                BSKY_RUNTIME_CHECK(!result &&
                                   result.error().category() == ErrorCategory::TimedOut);
                ++callbacks;
            },
            options);
        BSKY_RUNTIME_CHECK(timed_out);
        Access::force_timeout(timed_out.value());
        BSKY_RUNTIME_CHECK(runtime.process_once());
        BSKY_RUNTIME_CHECK(executor->run_one());
        BSKY_RUNTIME_CHECK(timed_out.value().error_category() == ErrorCategory::TimedOut);
        BSKY_RUNTIME_CHECK(callbacks == 2);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // Shutdown while a timeout callback is already running waits for that
    // callback to quiesce and preserves the TimedOut outcome.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto runtime = external(2U, executor);
        Gate callback_entered, release_callback;
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() + 1s;
        std::atomic<int> callbacks{0};
        auto operation = runtime.submit(
            success(),
            [&](Result<void> result) {
                BSKY_RUNTIME_CHECK(!result &&
                                   result.error().category() == ErrorCategory::TimedOut);
                callback_entered.signal();
                release_callback.wait();
                ++callbacks;
            },
            options);
        BSKY_RUNTIME_CHECK(operation);
        Access::force_timeout(operation.value());
        BSKY_RUNTIME_CHECK(runtime.process_once());
        std::thread callback_runner([&] { BSKY_RUNTIME_CHECK(executor->run_one()); });
        callback_entered.wait();
        std::optional<Result<void>> shutdown_result;
        std::thread stopper([&] { shutdown_result.emplace(runtime.shutdown(1s)); });
        release_callback.signal();
        callback_runner.join();
        stopper.join();
        BSKY_RUNTIME_CHECK(shutdown_result.has_value() && *shutdown_result);
        BSKY_RUNTIME_CHECK(callbacks == 1 &&
                           operation.value().error_category() == ErrorCategory::TimedOut &&
                           operation.value().await_callback_quiescence(0ms));
    }

    // Managed mode is driven by the same delayed TaskManager entry; no
    // private timer thread is needed for an operation deadline.
    {
        Runtime runtime;
        BSKY_RUNTIME_CHECK(runtime.start());
        std::atomic<int> callbacks{0};
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() - 1ms;
        auto operation = runtime.submit(
            success(),
            [&callbacks](Result<void> result) {
                BSKY_RUNTIME_CHECK(!result &&
                                   result.error().category() == ErrorCategory::TimedOut);
                ++callbacks;
            },
            options);
        BSKY_RUNTIME_CHECK(operation);
        BSKY_RUNTIME_CHECK(operation.value().wait_for_completion(1s));
        BSKY_RUNTIME_CHECK(operation.value().await_callback_quiescence(1s));
        BSKY_RUNTIME_CHECK(callbacks == 1);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // Closing a queued timeout callback suppresses delivery without changing
    // the already committed TimedOut terminal state.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto runtime = external(2U, executor);
        std::atomic<int> callbacks{0};
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() - 1ms;
        auto operation = runtime.submit(
            success(),
            [&callbacks](Result<void>) { ++callbacks; },
            options);
        BSKY_RUNTIME_CHECK(operation && operation.value().is_completed());
        operation.value().close_callback();
        BSKY_RUNTIME_CHECK(operation.value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(executor->run_one());
        BSKY_RUNTIME_CHECK(callbacks == 0 &&
                           operation.value().error_category() == ErrorCategory::TimedOut);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // The injected executor covers timeout acceptance, rejection, and an
    // accepted task that is dropped before dispatch.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto runtime = external(4U, executor);
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() + 1s;
        std::atomic<int> callbacks{0};

        auto accepted = runtime.submit(
            success(),
            [&callbacks](Result<void> result) {
                BSKY_RUNTIME_CHECK(!result &&
                                   result.error().category() == ErrorCategory::TimedOut);
                ++callbacks;
            },
            options);
        BSKY_RUNTIME_CHECK(accepted);
        Access::force_timeout(accepted.value());
        BSKY_RUNTIME_CHECK(runtime.process_once() && executor->run_one());
        BSKY_RUNTIME_CHECK(accepted.value().error_category() == ErrorCategory::TimedOut &&
                           callbacks == 1);

        executor->set_state(ControlledExecutor::State::Reject);
        auto rejected = runtime.submit(success(),
            [&callbacks](Result<void>) { ++callbacks; }, options);
        BSKY_RUNTIME_CHECK(rejected);
        Access::force_timeout(rejected.value());
        BSKY_RUNTIME_CHECK(runtime.process_once());
        BSKY_RUNTIME_CHECK(rejected.value().error_category() == ErrorCategory::TimedOut &&
                           rejected.value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(callbacks == 1);

        executor->set_state(ControlledExecutor::State::Accept);
        auto dropped = runtime.submit(success(),
            [&callbacks](Result<void>) { ++callbacks; }, options);
        BSKY_RUNTIME_CHECK(dropped);
        Access::force_timeout(dropped.value());
        BSKY_RUNTIME_CHECK(runtime.process_once() && executor->size() == 1U);
        executor->set_state(ControlledExecutor::State::Closed);
        BSKY_RUNTIME_CHECK(dropped.value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(dropped.value().error_category() == ErrorCategory::TimedOut &&
                           callbacks == 1);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // A delayed-task registration can be rejected after admission when the
    // upstream task manager is already closed.  This remains an admitted
    // handle with a typed Shutdown outcome and no user work.
    {
        auto runtime = external();
        Access::close_executor(runtime);
        std::atomic<bool> work_ran{false};
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() + 1s;
        auto operation = runtime.submit(
            [&work_ran](CancellationToken) {
                work_ran = true;
                return Result<void>::success();
            },
            {},
            options);
        BSKY_RUNTIME_CHECK(operation && operation.value().is_completed());
        BSKY_RUNTIME_CHECK(operation.value().error_category() == ErrorCategory::Shutdown);
        BSKY_RUNTIME_CHECK(!work_ran.load() &&
                           operation.value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // Shutdown suppresses a timeout callback that is still queued on an
    // injected executor; it does not manufacture a second terminal outcome.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto runtime = external(2U, executor);
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() - 1ms;
        std::atomic<int> callbacks{0};
        auto operation = runtime.submit(
            success(),
            [&callbacks](Result<void>) { ++callbacks; },
            options);
        BSKY_RUNTIME_CHECK(operation && executor->size() == 1U);
        BSKY_RUNTIME_CHECK(runtime.shutdown(0ms));
        BSKY_RUNTIME_CHECK(operation.value().error_category() == ErrorCategory::TimedOut);
        BSKY_RUNTIME_CHECK(operation.value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(callbacks == 0);
        executor->set_state(ControlledExecutor::State::Closed);
    }

    // Shutdown can win before the timer transition.  An attempted timeout
    // afterwards is harmless and cannot recreate a callback or registry entry.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto runtime = external(2U, executor);
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() + 1s;
        std::atomic<int> callbacks{0};
        auto operation = runtime.submit(
            success(),
            [&callbacks](Result<void>) { ++callbacks; },
            options);
        BSKY_RUNTIME_CHECK(operation);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
        Access::force_timeout(operation.value());
        BSKY_RUNTIME_CHECK(operation.value().error_category() == ErrorCategory::Shutdown);
        BSKY_RUNTIME_CHECK(callbacks == 0 &&
                           operation.value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(Access::registered_operations(runtime) == 0U);
    }
}

void blocking_transport_bridge_semantics() {
    // The bridge is explicitly opt-in and validates its transport before
    // admission; these synchronous failures do not dispatch callbacks.
    {
        auto runtime = external(2U);
        auto disabled = runtime.submit_blocking(
            {}, blocking_request(), [](Result<TransportResponse>) {
                BSKY_RUNTIME_CHECK(false);
            });
        BSKY_RUNTIME_CHECK(!disabled &&
                           disabled.error().category() == ErrorCategory::InvalidArgument);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // A blocking send runs on the bounded private worker, while the external
    // hub lane remains available for ordinary work and deadline dispatch.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto entered = std::make_shared<Gate>();
        auto release = std::make_shared<Gate>();
        auto transport_calls = std::make_shared<std::atomic<int>>(0);
        auto transport = std::make_shared<FakeTransport>(
            [entered, release, transport_calls](const TransportRequest&,
                                                  CancellationToken token) {
                const auto call = transport_calls->fetch_add(1) + 1;
                if (call == 1) {
                    entered->signal();
                    release->wait();
                }
                if (token.is_cancellation_requested()) {
                    return Result<TransportResponse>::failure(
                        Error(ErrorCategory::Cancelled, "cancelled", "test"));
                }
                auto response = TransportResponse::create(200U);
                BSKY_RUNTIME_CHECK(response);
                response.value().set_body(std::vector<std::uint8_t>{7U, 8U});
                return response;
            });
        auto runtime = blocking_external(4U, 2U, executor);
        std::atomic<int> bridge_callbacks{0};
        auto operation = runtime.submit_blocking(
            transport,
            blocking_request(),
            [&bridge_callbacks](Result<TransportResponse> response) {
                BSKY_RUNTIME_CHECK(response && response.value().status() == 200U);
                BSKY_RUNTIME_CHECK(response.value().body() ==
                                   std::vector<std::uint8_t>({7U, 8U}));
                ++bridge_callbacks;
            });
        BSKY_RUNTIME_CHECK(operation);
        entered->wait();

        std::atomic<int> regular_callbacks{0};
        auto regular = runtime.submit(
            success(), [&regular_callbacks](Result<void> result) {
                BSKY_RUNTIME_CHECK(result);
                ++regular_callbacks;
            });
        BSKY_RUNTIME_CHECK(regular && runtime.process_once());
        BSKY_RUNTIME_CHECK(regular.value().wait_for_completion(1s));
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        BSKY_RUNTIME_CHECK(regular_callbacks == 1);

        // The second bridge operation is queued behind the blocked send. Its
        // deadline must still be dispatched by the external hub before the
        // private worker is released; the queued transport call then observes
        // cancellation and never enters the adapter.
        OperationOptions deadline_options;
        deadline_options.deadline = Runtime::TimePoint::clock::now() + 100ms;
        std::atomic<int> timed_callbacks{0};
        auto timed = runtime.submit_blocking(
            transport,
            blocking_request(),
            [&timed_callbacks](Result<TransportResponse> result) {
                BSKY_RUNTIME_CHECK(!result &&
                                   result.error().category() == ErrorCategory::TimedOut);
                ++timed_callbacks;
            },
            deadline_options);
        BSKY_RUNTIME_CHECK(timed);
        std::this_thread::sleep_until(*deadline_options.deadline + 20ms);
        BSKY_RUNTIME_CHECK(!timed.value().is_completed());
        BSKY_RUNTIME_CHECK(runtime.process_once());
        BSKY_RUNTIME_CHECK(timed.value().wait_for_completion(1s) &&
                           timed.value().error_category() == ErrorCategory::TimedOut);
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        BSKY_RUNTIME_CHECK(timed_callbacks == 1);

        release->signal();
        BSKY_RUNTIME_CHECK(operation.value().wait_for_completion(1s));
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        BSKY_RUNTIME_CHECK(bridge_callbacks == 1 &&
                           operation.value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(timed.value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(transport_calls->load() == 1);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // Managed mode must keep the background hub responsive through the same
    // blocked-worker interleaving. The queued deadline expires on the hub,
    // then the worker is released and observes the committed timeout.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto entered = std::make_shared<Gate>();
        auto release = std::make_shared<Gate>();
        auto transport_calls = std::make_shared<std::atomic<int>>(0);
        auto transport = std::make_shared<FakeTransport>(
            [entered, release, transport_calls](const TransportRequest&,
                                                  CancellationToken token) {
                const auto call = transport_calls->fetch_add(1) + 1;
                if (call == 1) {
                    entered->signal();
                    release->wait();
                }
                if (token.is_cancellation_requested()) {
                    return Result<TransportResponse>::failure(
                        Error(ErrorCategory::Cancelled, "cancelled", "test"));
                }
                return TransportResponse::create(200U);
            });
        Runtime runtime({RuntimeMode::Managed, 4U, {}, executor, 2U});
        BSKY_RUNTIME_CHECK(runtime.start());
        std::atomic<int> bridge_callbacks{0};
        auto operation = runtime.submit_blocking(
            transport,
            blocking_request(),
            [&bridge_callbacks](Result<TransportResponse> result) {
                BSKY_RUNTIME_CHECK(result && result.value().status() == 200U);
                ++bridge_callbacks;
            });
        BSKY_RUNTIME_CHECK(operation);
        entered->wait();

        std::atomic<int> regular_callbacks{0};
        auto regular = runtime.submit(
            success(), [&regular_callbacks](Result<void> result) {
                BSKY_RUNTIME_CHECK(result);
                ++regular_callbacks;
            });
        BSKY_RUNTIME_CHECK(regular &&
                           regular.value().wait_for_completion(1s));
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        BSKY_RUNTIME_CHECK(regular_callbacks == 1);

        OperationOptions deadline_options;
        deadline_options.deadline = Runtime::TimePoint::clock::now() + 100ms;
        std::atomic<int> timed_callbacks{0};
        auto timed = runtime.submit_blocking(
            transport,
            blocking_request(),
            [&timed_callbacks](Result<TransportResponse> result) {
                BSKY_RUNTIME_CHECK(!result &&
                                   result.error().category() == ErrorCategory::TimedOut);
                ++timed_callbacks;
            },
            deadline_options);
        BSKY_RUNTIME_CHECK(timed &&
                           timed.value().wait_for_completion(1s) &&
                           timed.value().error_category() == ErrorCategory::TimedOut);
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        BSKY_RUNTIME_CHECK(timed_callbacks == 1);

        release->signal();
        BSKY_RUNTIME_CHECK(operation.value().wait_for_completion(1s));
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        BSKY_RUNTIME_CHECK(bridge_callbacks == 1 &&
                           operation.value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(timed.value().await_callback_quiescence(0ms));
        BSKY_RUNTIME_CHECK(transport_calls->load() == 1);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // The queue is bounded by admitted blocking operations, not by an
    // unbounded TaskManager queue. A single private worker also prevents
    // concurrent blocking sends in this bridge.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto entered = std::make_shared<Gate>();
        auto release = std::make_shared<Gate>();
        auto active = std::make_shared<std::atomic<int>>(0);
        auto maximum = std::make_shared<std::atomic<int>>(0);
        auto transport = std::make_shared<FakeTransport>(
            [entered, release, active, maximum](const TransportRequest&,
                                                 CancellationToken token) {
                const auto now = active->fetch_add(1) + 1;
                auto old = maximum->load();
                while (now > old &&
                       !maximum->compare_exchange_weak(old, now)) {}
                entered->signal();
                release->wait();
                active->fetch_sub(1);
                if (token.is_cancellation_requested()) {
                    return Result<TransportResponse>::failure(
                        Error(ErrorCategory::Cancelled, "cancelled", "test"));
                }
                return TransportResponse::create(204U);
            });
        auto runtime = blocking_external(4U, 1U, executor);
        auto first = runtime.submit_blocking(transport, blocking_request());
        BSKY_RUNTIME_CHECK(first);
        entered->wait();
        auto full = runtime.submit_blocking(transport, blocking_request());
        BSKY_RUNTIME_CHECK(!full &&
                           full.error().category() == ErrorCategory::Backpressure);
        release->signal();
        BSKY_RUNTIME_CHECK(first.value().wait_for_completion(1s));
        BSKY_RUNTIME_CHECK(maximum->load() == 1);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // Cancellation before a queued blocking job reaches the private worker
    // prevents the transport call from starting while still delivering the
    // single terminal callback.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto entered = std::make_shared<Gate>();
        auto release = std::make_shared<Gate>();
        auto transport_calls = std::make_shared<std::atomic<int>>(0);
        auto transport = std::make_shared<FakeTransport>(
            [entered, release, transport_calls](const TransportRequest&,
                                                  CancellationToken token) {
                const auto call = transport_calls->fetch_add(1) + 1;
                if (call == 1) {
                    entered->signal();
                    release->wait();
                }
                if (token.is_cancellation_requested()) {
                    return Result<TransportResponse>::failure(
                        Error(ErrorCategory::Cancelled, "cancelled", "test"));
                }
                return TransportResponse::create(200U);
            });
        auto runtime = blocking_external(4U, 2U, executor);
        auto first = runtime.submit_blocking(transport, blocking_request());
        BSKY_RUNTIME_CHECK(first);
        entered->wait();
        std::atomic<int> cancelled_callbacks{0};
        auto queued = runtime.submit_blocking(
            transport, blocking_request(),
            [&cancelled_callbacks](Result<TransportResponse> result) {
                BSKY_RUNTIME_CHECK(!result &&
                                   result.error().category() == ErrorCategory::Cancelled);
                ++cancelled_callbacks;
            });
        BSKY_RUNTIME_CHECK(queued);
        queued.value().request_cancellation();
        BSKY_RUNTIME_CHECK(queued.value().error_category() ==
                           ErrorCategory::Cancelled);
        release->signal();
        BSKY_RUNTIME_CHECK(first.value().wait_for_completion(1s));
        BSKY_RUNTIME_CHECK(queued.value().await_callback_quiescence(0ms) == false);
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        BSKY_RUNTIME_CHECK(queued.value().await_callback_quiescence(1s));
        BSKY_RUNTIME_CHECK(cancelled_callbacks == 1);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
        BSKY_RUNTIME_CHECK(transport_calls->load() == 1);
    }

    // Cancellation and timeout commit the shared first-winner outcome while
    // the transport remains blocked; the transport observes the token only
    // after the gate is released and is never forcibly interrupted.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto entered = std::make_shared<Gate>();
        auto release = std::make_shared<Gate>();
        auto transport = std::make_shared<FakeTransport>(
            [entered, release](const TransportRequest&, CancellationToken token) {
                entered->signal();
                release->wait();
                if (token.is_cancellation_requested()) {
                    return Result<TransportResponse>::failure(
                        Error(ErrorCategory::Cancelled, "cancelled", "test"));
                }
                return TransportResponse::create(200U);
            });
        auto runtime = blocking_external(4U, 2U, executor);
        std::atomic<int> callbacks{0};
        auto cancelled = runtime.submit_blocking(
            transport, blocking_request(),
            [&callbacks](Result<TransportResponse> result) {
                BSKY_RUNTIME_CHECK(!result &&
                                   result.error().category() == ErrorCategory::Cancelled);
                ++callbacks;
            });
        BSKY_RUNTIME_CHECK(cancelled);
        entered->wait();
        cancelled.value().request_cancellation();
        BSKY_RUNTIME_CHECK(cancelled.value().error_category() ==
                           ErrorCategory::Cancelled);
        release->signal();
        BSKY_RUNTIME_CHECK(cancelled.value().wait_for_completion(1s));
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        BSKY_RUNTIME_CHECK(callbacks == 1);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto entered = std::make_shared<Gate>();
        auto release = std::make_shared<Gate>();
        auto finished = std::make_shared<Gate>();
        auto observed_cancel = std::make_shared<std::atomic<bool>>(false);
        auto transport = std::make_shared<FakeTransport>(
            [entered, release, finished, observed_cancel](
                const TransportRequest&, CancellationToken token) {
                entered->signal();
                release->wait();
                observed_cancel->store(token.is_cancellation_requested());
                finished->signal();
                return TransportResponse::create(200U);
            });
        auto runtime = blocking_external(4U, 2U, executor);
        std::atomic<int> callbacks{0};
        auto timed = runtime.submit_blocking(
            transport, blocking_request(),
            [&callbacks](Result<TransportResponse> result) {
                BSKY_RUNTIME_CHECK(!result &&
                                   result.error().category() == ErrorCategory::TimedOut);
                ++callbacks;
            });
        BSKY_RUNTIME_CHECK(timed);
        entered->wait();
        Access::force_timeout(timed.value());
        BSKY_RUNTIME_CHECK(timed.value().error_category() == ErrorCategory::TimedOut);
        BSKY_RUNTIME_CHECK(timed.value().wait_for_completion(1s));
        release->signal();
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        finished->wait();
        BSKY_RUNTIME_CHECK(timed.value().await_callback_quiescence(1s));
        BSKY_RUNTIME_CHECK(observed_cancel->load() && callbacks == 1);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // Shutdown reports a bounded wait while a blocking send remains in flight,
    // then succeeds after the cooperative worker returns. Queued callbacks are
    // suppressed; callback code is never run inline by shutdown().
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto entered = std::make_shared<Gate>();
        auto release = std::make_shared<Gate>();
        auto transport = std::make_shared<FakeTransport>(
            [entered, release](const TransportRequest&, CancellationToken) {
                entered->signal();
                release->wait();
                return TransportResponse::create(200U);
            });
        auto runtime = blocking_external(4U, 2U, executor);
        auto operation = runtime.submit_blocking(
            transport, blocking_request(), [](Result<TransportResponse>) {
                BSKY_RUNTIME_CHECK(false);
            });
        BSKY_RUNTIME_CHECK(operation);
        entered->wait();
        auto pending = runtime.shutdown(10ms);
        BSKY_RUNTIME_CHECK(!pending &&
                           pending.error().category() == ErrorCategory::ShutdownTimedOut);
        release->signal();
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
        BSKY_RUNTIME_CHECK(operation.value().error_category() ==
                           ErrorCategory::Shutdown);
    }

    // Managed shutdown must wake the hub loop when the private worker returns
    // the last admitted operation. Without that wakeup, the managed loop can
    // remain asleep after a blocked send is released and the final shutdown
    // wait expires even though all work is quiescent.
    {
        auto entered = std::make_shared<Gate>();
        auto release = std::make_shared<Gate>();
        auto returned = std::make_shared<Gate>();
        auto transport = std::make_shared<FakeTransport>(
            [entered, release, returned](const TransportRequest&, CancellationToken) {
                entered->signal();
                release->wait();
                returned->signal();
                return TransportResponse::create(200U);
            });
        Runtime runtime({RuntimeMode::Managed, 4U, {}, {}, 1U});
        BSKY_RUNTIME_CHECK(runtime.start());
        auto operation = runtime.submit_blocking(transport, blocking_request());
        BSKY_RUNTIME_CHECK(operation);
        entered->wait();
        auto pending = runtime.shutdown(10ms);
        BSKY_RUNTIME_CHECK(!pending &&
                           pending.error().category() == ErrorCategory::ShutdownTimedOut);
        release->signal();
        returned->wait();
        BSKY_RUNTIME_CHECK(operation.value().wait_for_completion(1s));
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // A throwing transport is contained as Internal.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto transport = std::make_shared<ThrowingTransport>();
        auto runtime = blocking_external(4U, 2U, executor);
        std::atomic<int> callbacks{0};
        auto operation = runtime.submit_blocking(
            transport, blocking_request(),
            [&callbacks](Result<TransportResponse> result) {
                BSKY_RUNTIME_CHECK(!result &&
                                   result.error().category() == ErrorCategory::Internal);
                BSKY_RUNTIME_CHECK(result.error().message().find("secret") ==
                                   std::string::npos);
                ++callbacks;
            });
        BSKY_RUNTIME_CHECK(operation &&
                           operation.value().wait_for_completion(1s));
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        BSKY_RUNTIME_CHECK(callbacks == 1 && runtime.shutdown(1s));
    }

    // A worker rejection after admission is one Shutdown terminal outcome.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto transport = std::make_shared<FakeTransport>(
            [](const TransportRequest&, CancellationToken) {
                return TransportResponse::create(200U);
            });
        auto runtime = blocking_external(4U, 2U, executor);
        Access::close_blocking_worker(runtime);
        std::atomic<int> rejection_callbacks{0};
        auto rejected = runtime.submit_blocking(
            transport, blocking_request(),
            [&rejection_callbacks](Result<TransportResponse> result) {
                BSKY_RUNTIME_CHECK(!result &&
                                   result.error().category() == ErrorCategory::Shutdown);
                ++rejection_callbacks;
            });
        BSKY_RUNTIME_CHECK(rejected && rejected.value().is_completed());
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        BSKY_RUNTIME_CHECK(rejection_callbacks == 1);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    {
        auto executor = std::make_shared<ControlledExecutor>();
        executor->set_state(ControlledExecutor::State::Reject);
        auto transport = std::make_shared<FakeTransport>(
            [](const TransportRequest&, CancellationToken) {
                return TransportResponse::create(200U);
            });
        auto runtime = blocking_external(4U, 2U, executor);
        std::atomic<int> callbacks{0};
        auto operation = runtime.submit_blocking(
            transport, blocking_request(),
            [&callbacks](Result<TransportResponse>) { ++callbacks; });
        BSKY_RUNTIME_CHECK(operation);
        BSKY_RUNTIME_CHECK(operation.value().wait_for_completion(1s));
        BSKY_RUNTIME_CHECK(operation.value().await_callback_quiescence(1s));
        BSKY_RUNTIME_CHECK(callbacks == 0 &&
                           !operation.value().error_category());
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }
}

void native_async_transport_semantics() {
    // Native adapter admission is synchronous, while the terminal callback is
    // still delivered only through the selected executor.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto pending = std::make_shared<AsyncTransport::Completion>();
        auto transport = std::make_shared<FakeAsyncTransport>(
            [pending](TransportRequest request, CancellationToken,
                      AsyncTransport::Completion completion) {
                BSKY_RUNTIME_CHECK(request.url().find("/xrpc/") != std::string::npos);
                *pending = std::move(completion);
                return Result<void>::success();
            });
        auto runtime = external(4U, executor);
        std::atomic<int> callbacks{0};
        auto operation = runtime.submit_async(
            transport, blocking_request(),
            [&callbacks](Result<TransportResponse> response) {
                BSKY_RUNTIME_CHECK(response && response.value().status() == 200U);
                ++callbacks;
            });
        BSKY_RUNTIME_CHECK(operation && !operation.value().is_completed());
        BSKY_RUNTIME_CHECK(transport->requests().size() == 1U);
        auto response = TransportResponse::create(200U);
        BSKY_RUNTIME_CHECK(response);
        (*pending)(std::move(response));
        BSKY_RUNTIME_CHECK(operation.value().wait_for_completion(1s));
        BSKY_RUNTIME_CHECK(callbacks == 0);

        // A duplicate completion cannot replace the first payload even while
        // callback delivery is still queued.
        auto duplicate = TransportResponse::create(201U);
        BSKY_RUNTIME_CHECK(duplicate);
        (*pending)(std::move(duplicate));
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        BSKY_RUNTIME_CHECK(callbacks == 1 &&
                           operation.value().await_callback_quiescence(0ms));

        // A late completion after callback quiescence is ignored as well.
        auto late = TransportResponse::create(202U);
        BSKY_RUNTIME_CHECK(late);
        (*pending)(std::move(late));
        BSKY_RUNTIME_CHECK(callbacks == 1);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // A cancellation wins before the adapter completion and suppresses a late
    // response without requiring a waiter thread or a second scheduler.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto pending = std::make_shared<AsyncTransport::Completion>();
        auto transport = std::make_shared<FakeAsyncTransport>(
            [pending](TransportRequest, CancellationToken,
                      AsyncTransport::Completion completion) {
                *pending = std::move(completion);
                return Result<void>::success();
            });
        auto runtime = external(4U, executor);
        std::atomic<int> callbacks{0};
        auto operation = runtime.submit_async(
            transport, blocking_request(),
            [&callbacks](Result<TransportResponse> response) {
                BSKY_RUNTIME_CHECK(!response &&
                                   response.error().category() == ErrorCategory::Cancelled);
                ++callbacks;
            });
        BSKY_RUNTIME_CHECK(operation);
        operation.value().request_cancellation();
        BSKY_RUNTIME_CHECK(operation.value().error_category() == ErrorCategory::Cancelled);
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        auto late = TransportResponse::create(200U);
        BSKY_RUNTIME_CHECK(late);
        (*pending)(std::move(late));
        BSKY_RUNTIME_CHECK(callbacks == 1);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // Adapter admission failures are synchronous and never invoke the user
    // callback. This is distinct from an accepted transport error completion.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto transport = std::make_shared<FakeAsyncTransport>();
        auto runtime = external(4U, executor);
        std::atomic<int> callbacks{0};
        auto rejected = runtime.submit_async(
            transport, blocking_request(),
            [&callbacks](Result<TransportResponse>) { ++callbacks; });
        BSKY_RUNTIME_CHECK(!rejected &&
                           rejected.error().category() == ErrorCategory::Transport);
        BSKY_RUNTIME_CHECK(callbacks == 0 && Access::registered_operations(runtime) == 0U);
        BSKY_RUNTIME_CHECK(runtime.shutdown(1s));
    }

    // An inline adapter completion still reaches the user asynchronously.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto transport = std::make_shared<FakeAsyncTransport>(
            [](TransportRequest, CancellationToken,
               AsyncTransport::Completion completion) {
                auto response = TransportResponse::create(204U);
                BSKY_RUNTIME_CHECK(response);
                completion(std::move(response));
                return Result<void>::success();
            });
        auto runtime = external(4U, executor);
        std::atomic<int> callbacks{0};
        auto operation = runtime.submit_async(
            transport, blocking_request(),
            [&callbacks](Result<TransportResponse> response) {
                BSKY_RUNTIME_CHECK(response && response.value().status() == 204U);
                ++callbacks;
            });
        BSKY_RUNTIME_CHECK(operation && callbacks == 0);
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        BSKY_RUNTIME_CHECK(callbacks == 1 && runtime.shutdown(1s));
    }

    // An expired deadline commits before adapter dispatch and therefore does
    // not call the native transport at all.
    {
        auto executor = std::make_shared<ControlledExecutor>();
        auto transport = std::make_shared<FakeAsyncTransport>(
            [](TransportRequest, CancellationToken, AsyncTransport::Completion) {
                BSKY_RUNTIME_CHECK(false);
                return Result<void>::success();
            });
        auto runtime = external(4U, executor);
        OperationOptions options;
        options.deadline = Runtime::TimePoint::clock::now() - 1ms;
        std::atomic<int> callbacks{0};
        auto operation = runtime.submit_async(
            transport, blocking_request(),
            [&callbacks](Result<TransportResponse> response) {
                BSKY_RUNTIME_CHECK(!response &&
                                   response.error().category() == ErrorCategory::TimedOut);
                ++callbacks;
            },
            options);
        BSKY_RUNTIME_CHECK(operation && operation.value().is_completed());
        BSKY_RUNTIME_CHECK(transport->requests().empty());
        BSKY_RUNTIME_CHECK(run_one_when_ready(*executor));
        BSKY_RUNTIME_CHECK(callbacks == 1 && runtime.shutdown(1s));
    }
}
} // namespace

int main() {
    admission_capacity_and_terminal_races();
    close_and_move_only_captures();
    queue_rejection_is_terminal();
    startup_stop_lifecycle();
    external_stop_without_loop();
    for (auto mode : {RuntimeMode::External, RuntimeMode::Managed}) {
        running_work_and_callback_quiescence(false, mode);
        running_work_and_callback_quiescence(true, mode);
    }
    concurrent_admission_cancel_stop();
    racing_admission_cancel_stop();
    terminal_allocation_failure();
    execute_terminal_faults();
    independent_runtimes_and_concurrent_stop();
    external_finalization_handoff();
    self_shutdown_and_destruction(RuntimeMode::External);
    self_shutdown_and_destruction(RuntimeMode::Managed);
    notifier_and_move_assignment();
    typed_operations();
    typed_callback_lifetime();
    typed_terminal_races_and_faults();
    injected_executor_lifecycle();
    operation_deadline_semantics();
    blocking_transport_bridge_semantics();
    native_async_transport_semantics();
}
