#include "bsky_cpp/runtime/runtime.hpp"
#include "bsky_cpp/runtime/detail/runtime_test_access.hpp"
#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
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
        if (reject_work) Access::close_executor(runtime);
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
    BSKY_RUNTIME_CHECK(!pending && pending.error().category() == ErrorCategory::Shutdown);
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
        });
        std::thread canceller([&] {
            start.wait();
            initial.value().request_cancellation();
            initial.value().request_cancellation();
        });
        std::thread stopper([&] { start.wait(); BSKY_RUNTIME_CHECK(runtime.shutdown(1s)); });
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
        BSKY_RUNTIME_CHECK(!pending && pending.error().category() == ErrorCategory::Shutdown);
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
    self_shutdown_and_destruction(RuntimeMode::External);
    self_shutdown_and_destruction(RuntimeMode::Managed);
    notifier_and_move_assignment();
}
