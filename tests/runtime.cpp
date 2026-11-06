#include "bsky_cpp/runtime/runtime.hpp"
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

Runtime external(std::size_t limit = 2U) {
    Runtime runtime({RuntimeMode::External, limit, {}});
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
        BSKY_RUNTIME_CHECK(result.value().error_category() == ErrorCategory::Shutdown);
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
    BSKY_RUNTIME_CHECK(callback_rejection.value().error_category() == ErrorCategory::Shutdown);
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
} // namespace

int main() {
    admission_capacity_and_terminal_races();
    close_and_move_only_captures();
    queue_rejection_is_terminal();
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
}
