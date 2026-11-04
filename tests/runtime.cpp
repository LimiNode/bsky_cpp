#include "bsky_cpp/runtime/runtime.hpp"

#include "test_support.hpp"

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace {

using namespace std::chrono_literals;

void external_runtime() {
    bsky_cpp::runtime::Runtime runtime({bsky_cpp::runtime::RuntimeMode::External, 1U});
    BSKY_TEST_CHECK(static_cast<bool>(runtime.start()));
    BSKY_TEST_CHECK(runtime.accepting());

    std::atomic<bool> callback_called{false};
    auto first = runtime.submit(
        bsky_cpp::runtime::OperationWork(
            [](bsky_cpp::CancellationToken) {
                return bsky_cpp::Result<void>::success();
            }),
        bsky_cpp::runtime::CompletionCallback(
            [&](bsky_cpp::Result<void> result) {
                BSKY_TEST_CHECK(result.has_value());
                callback_called.store(true, std::memory_order_release);
            }));
    BSKY_TEST_CHECK(static_cast<bool>(first));

    auto rejected = runtime.submit(
        bsky_cpp::runtime::OperationWork(
            [](bsky_cpp::CancellationToken) {
                return bsky_cpp::Result<void>::success();
            }));
    BSKY_TEST_CHECK(!rejected);
    BSKY_TEST_CHECK(rejected.error().category() ==
                    bsky_cpp::ErrorCategory::Backpressure);

    auto second = runtime.submit(
        bsky_cpp::runtime::OperationWork(
            [](bsky_cpp::CancellationToken) {
                return bsky_cpp::Result<void>::success();
            }));
    BSKY_TEST_CHECK(!second);

    auto handle = std::move(first.value());
    for (int pass = 0; pass != 8 && !handle.is_completed(); ++pass) {
        BSKY_TEST_CHECK(static_cast<bool>(runtime.process_once()));
    }
    BSKY_TEST_CHECK(handle.wait_for_completion(100ms));
    BSKY_TEST_CHECK(handle.is_completed());
    for (int pass = 0; pass != 8 && !callback_called.load(std::memory_order_acquire); ++pass) {
        (void)runtime.process_once();
    }
    BSKY_TEST_CHECK(callback_called.load(std::memory_order_acquire));

    bool suppressed_called = false;
    auto suppressed = runtime.submit(
        bsky_cpp::runtime::OperationWork(
            [](bsky_cpp::CancellationToken) {
                return bsky_cpp::Result<void>::success();
            }),
        bsky_cpp::runtime::CompletionCallback(
            [&](bsky_cpp::Result<void>) { suppressed_called = true; }));
    BSKY_TEST_CHECK(static_cast<bool>(suppressed));
    auto suppressed_handle = std::move(suppressed.value());
    suppressed_handle.close_callback();
    for (int pass = 0; pass != 8 && !suppressed_handle.is_completed(); ++pass) {
        (void)runtime.process_once();
    }
    BSKY_TEST_CHECK(suppressed_handle.wait_for_completion(100ms));
    BSKY_TEST_CHECK(suppressed_handle.await_callback_quiescence(100ms));
    BSKY_TEST_CHECK(!suppressed_called);

    bool cancellation_callback = false;
    auto cancelled = runtime.submit(
        bsky_cpp::runtime::OperationWork(
            [](bsky_cpp::CancellationToken) {
                return bsky_cpp::Result<void>::success();
            }),
        bsky_cpp::runtime::CompletionCallback(
            [&](bsky_cpp::Result<void> result) {
                cancellation_callback =
                    !result.has_value() &&
                    result.error().category() == bsky_cpp::ErrorCategory::Cancelled;
            }));
    BSKY_TEST_CHECK(static_cast<bool>(cancelled));
    auto cancelled_handle = std::move(cancelled.value());
    cancelled_handle.request_cancellation();
    for (int pass = 0; pass != 8 && !cancelled_handle.is_completed(); ++pass) {
        (void)runtime.process_once();
    }
    BSKY_TEST_CHECK(cancelled_handle.wait_for_completion(100ms));
    for (int pass = 0; pass != 8 && !cancellation_callback; ++pass) {
        (void)runtime.process_once();
    }
    BSKY_TEST_CHECK(cancellation_callback);
    BSKY_TEST_CHECK(static_cast<bool>(runtime.shutdown(1s)));
    BSKY_TEST_CHECK(!runtime.accepting());
    auto after_shutdown = runtime.submit(
        bsky_cpp::runtime::OperationWork(
            [](bsky_cpp::CancellationToken) {
                return bsky_cpp::Result<void>::success();
            }));
    BSKY_TEST_CHECK(!after_shutdown);
    BSKY_TEST_CHECK(after_shutdown.error().category() ==
                    bsky_cpp::ErrorCategory::Shutdown);
    BSKY_TEST_CHECK(static_cast<bool>(runtime.shutdown(1s)));
}

void managed_runtime() {
    bsky_cpp::runtime::Runtime runtime;
    BSKY_TEST_CHECK(static_cast<bool>(runtime.start()));

    std::mutex mutex;
    std::condition_variable condition;
    bool callback_called = false;
    auto submitted = runtime.submit(
        bsky_cpp::runtime::OperationWork(
            [](bsky_cpp::CancellationToken) {
                return bsky_cpp::Result<void>::success();
            }),
        bsky_cpp::runtime::CompletionCallback(
            [&](bsky_cpp::Result<void> result) {
                BSKY_TEST_CHECK(result.has_value());
                std::lock_guard<std::mutex> lock(mutex);
                callback_called = true;
                condition.notify_all();
            }));
    BSKY_TEST_CHECK(static_cast<bool>(submitted));
    auto handle = std::move(submitted.value());
    BSKY_TEST_CHECK(handle.wait_for_completion(1s));
    std::unique_lock<std::mutex> lock(mutex);
    BSKY_TEST_CHECK(condition.wait_for(lock, 1s, [&] { return callback_called; }));
    lock.unlock();
    BSKY_TEST_CHECK(handle.await_callback_quiescence(1s));
    BSKY_TEST_CHECK(static_cast<bool>(runtime.shutdown(1s)));
}

void external_shutdown_drains() {
    bsky_cpp::runtime::Runtime runtime(
        {bsky_cpp::runtime::RuntimeMode::External, 2U});
    BSKY_TEST_CHECK(static_cast<bool>(runtime.start()));

    std::atomic<int> callback_count{0};
    std::atomic<bool> cancelled{false};
    auto submitted = runtime.submit(
        bsky_cpp::runtime::OperationWork(
            [](bsky_cpp::CancellationToken) {
                return bsky_cpp::Result<void>::success();
            }),
        bsky_cpp::runtime::CompletionCallback(
            [&](bsky_cpp::Result<void> result) {
                if (!result.has_value() &&
                    result.error().category() == bsky_cpp::ErrorCategory::Cancelled) {
                    cancelled.store(true, std::memory_order_release);
                }
                callback_count.fetch_add(1, std::memory_order_acq_rel);
            }));
    BSKY_TEST_CHECK(static_cast<bool>(submitted));
    auto handle = std::move(submitted.value());

    // shutdown() owns the external drain phase; the caller does not need to
    // race a separate process_once() call to preserve an accepted operation.
    runtime.request_stop();
    BSKY_TEST_CHECK(static_cast<bool>(runtime.shutdown(1s)));
    BSKY_TEST_CHECK(handle.is_completed());
    BSKY_TEST_CHECK(callback_count.load(std::memory_order_acquire) == 1);
    BSKY_TEST_CHECK(cancelled.load(std::memory_order_acquire));
}

void managed_self_shutdown_is_bounded() {
    bsky_cpp::runtime::Runtime runtime;
    BSKY_TEST_CHECK(static_cast<bool>(runtime.start()));

    std::mutex mutex;
    std::condition_variable condition;
    bool callback_called = false;
    bool self_shutdown_timed_out = false;
    auto submitted = runtime.submit(
        bsky_cpp::runtime::OperationWork(
            [](bsky_cpp::CancellationToken) {
                return bsky_cpp::Result<void>::success();
            }),
        bsky_cpp::runtime::CompletionCallback(
            [&](bsky_cpp::Result<void>) {
                auto result = runtime.shutdown(20ms);
                std::lock_guard<std::mutex> lock(mutex);
                self_shutdown_timed_out =
                    !result && result.error().category() ==
                                   bsky_cpp::ErrorCategory::Shutdown;
                callback_called = true;
                condition.notify_all();
            }));
    BSKY_TEST_CHECK(static_cast<bool>(submitted));
    auto handle = std::move(submitted.value());
    BSKY_TEST_CHECK(handle.wait_for_completion(1s));
    std::unique_lock<std::mutex> lock(mutex);
    BSKY_TEST_CHECK(condition.wait_for(lock, 1s, [&] { return callback_called; }));
    lock.unlock();
    BSKY_TEST_CHECK(self_shutdown_timed_out);
    BSKY_TEST_CHECK(static_cast<bool>(runtime.shutdown(1s)));
}

}  // namespace

int main() {
    external_runtime();
    managed_runtime();
    external_shutdown_drains();
    managed_self_shutdown_is_bounded();
    return 0;
}
