#include "bsky_cpp/core/cancellation.hpp"
#include "bsky_cpp/core/bsky_error.hpp"
#include "bsky_cpp/core/result.hpp"

#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

struct MoveOnly {
    explicit MoveOnly(int value) : value(std::make_unique<int>(value)) {}
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;

    std::unique_ptr<int> value;
};

struct ThrowingMove {
    explicit ThrowingMove(int input) : value(input) {}
    ThrowingMove(const ThrowingMove&) = default;
    ThrowingMove& operator=(const ThrowingMove&) = default;
    ThrowingMove(ThrowingMove&& other) {
        if (throw_on_move) {
            throw std::runtime_error("intentional test move failure");
        }
        value = other.value;
    }
    ThrowingMove& operator=(ThrowingMove&& other) {
        if (throw_on_move) {
            throw std::runtime_error("intentional test move failure");
        }
        value = other.value;
        return *this;
    }

    static bool throw_on_move;
    int value;
};

bool ThrowingMove::throw_on_move = false;

bool result_success_and_error() {
    bsky_cpp::Result<MoveOnly> success(MoveOnly(7));
    if (!BSKY_TEST_CHECK(success.has_value())) {
        return false;
    }
    if (!BSKY_TEST_CHECK(*success.value().value == 7)) {
        return false;
    }

    bsky_cpp::Result<MoveOnly> moved(std::move(success));
    if (!BSKY_TEST_CHECK(moved.has_value())) {
        return false;
    }
    if (!BSKY_TEST_CHECK(*moved.value().value == 7)) {
        return false;
    }
    if (!BSKY_TEST_CHECK(success.has_value())) {
        return false;
    }
    if (!BSKY_TEST_CHECK(success.value().value == nullptr)) {
        return false;
    }

    bsky_cpp::Error error(
        bsky_cpp::ErrorCategory::Transport, "request failed", "read_profile");
    error.add_metadata("status", "503");
    error.add_metadata("Authorization", "Bearer secret");
    error.add_metadata("api_key", "secret");
    error.add_metadata("payload", "untrusted server body");
    error.add_metadata("status", "Bearer secret");
    error.add_metadata("uri", "https://user:secret@example.test/path?token=x");
    error.add_metadata("status_code", "Bearer secret");
    error.add_metadata("retry_after_ms", "1000");
    bsky_cpp::Result<MoveOnly> failure(std::move(error));
    if (!BSKY_TEST_CHECK(!failure.has_value())) {
        return false;
    }
    if (!BSKY_TEST_CHECK(failure.error().category() == bsky_cpp::ErrorCategory::Transport)) {
        return false;
    }
    if (!BSKY_TEST_CHECK(failure.error().metadata().size() == 8)) {
        return false;
    }
    if (!BSKY_TEST_CHECK(failure.error().metadata()[0].value == "503")) {
        return false;
    }
    if (!BSKY_TEST_CHECK(failure.error().metadata()[1].value == "<redacted>")) {
        return false;
    }
    if (!BSKY_TEST_CHECK(failure.error().metadata()[2].value == "<redacted>")) {
        return false;
    }
    if (!BSKY_TEST_CHECK(failure.error().metadata()[3].value == "<redacted>")) {
        return false;
    }
    if (!BSKY_TEST_CHECK(failure.error().metadata()[4].value == "<redacted>")) {
        return false;
    }
    if (!BSKY_TEST_CHECK(failure.error().metadata()[5].value == "<redacted>")) {
        return false;
    }
    if (!BSKY_TEST_CHECK(failure.error().metadata()[6].key == "<redacted>" &&
                         failure.error().metadata()[6].value == "<redacted>")) {
        return false;
    }
    if (!BSKY_TEST_CHECK(failure.error().metadata()[7].value == "1000")) {
        return false;
    }

    bsky_cpp::Error bounded(bsky_cpp::ErrorCategory::Internal, "bounded");
    for (int index = 0; index < 32; ++index) {
        bounded.add_metadata("status", "503");
    }
    if (!BSKY_TEST_CHECK(bounded.metadata().size() == 16)) {
        return false;
    }

    bsky_cpp::Result<void> void_success = bsky_cpp::Result<void>::success();
    if (!BSKY_TEST_CHECK(void_success.has_value())) {
        return false;
    }
    bsky_cpp::Result<void> void_failure = bsky_cpp::Result<void>::failure(
        bsky_cpp::Error(bsky_cpp::ErrorCategory::Cancelled, "cancelled"));
    if (!BSKY_TEST_CHECK(!void_failure.has_value())) {
        return false;
    }
    return BSKY_TEST_CHECK(void_failure.error().category() ==
                           bsky_cpp::ErrorCategory::Cancelled);
}

bool result_invalid_access() {
    bsky_cpp::Result<int> success(1);
    bool threw = false;
    try {
        (void)success.error();
    } catch (const std::logic_error&) {
        threw = true;
    }
    if (!BSKY_TEST_CHECK(threw)) {
        return false;
    }

    bsky_cpp::Result<int> failure(
        bsky_cpp::Error(bsky_cpp::ErrorCategory::Internal, "failure"));
    threw = false;
    try {
        (void)failure.value();
    } catch (const std::logic_error&) {
        threw = true;
    }
    if (!BSKY_TEST_CHECK(threw)) {
        return false;
    }

    return BSKY_TEST_CHECK(!std::is_move_assignable<bsky_cpp::Result<int>>::value);
}

bool result_exception_safety() {
    bsky_cpp::Result<ThrowingMove> source(ThrowingMove(9));
    ThrowingMove::throw_on_move = true;
    bool threw = false;
    try {
        bsky_cpp::Result<ThrowingMove> moved(std::move(source));
        (void)moved;
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ThrowingMove::throw_on_move = false;
    if (!BSKY_TEST_CHECK(threw)) {
        return false;
    }
    if (!BSKY_TEST_CHECK(source.has_value())) {
        return false;
    }
    return BSKY_TEST_CHECK(source.value().value == 9);
}

bool cancellation_owner_and_repeat() {
    bsky_cpp::CancellationToken token;
    {
        bsky_cpp::CancellationSource source;
        token = source.token();
        if (!BSKY_TEST_CHECK(!token.is_cancellation_requested())) {
            return false;
        }
    }
    if (!BSKY_TEST_CHECK(!token.is_cancellation_requested())) {
        return false;
    }

    bsky_cpp::CancellationSource source;
    token = source.token();
    if (!BSKY_TEST_CHECK(source.request_cancellation())) {
        return false;
    }
    if (!BSKY_TEST_CHECK(!source.request_cancellation())) {
        return false;
    }
    return BSKY_TEST_CHECK(token.is_cancellation_requested());
}

bool cancellation_moved_from_source() {
    bsky_cpp::CancellationSource source;
    const bsky_cpp::CancellationToken token = source.token();
    bsky_cpp::CancellationSource moved(std::move(source));
    if (!BSKY_TEST_CHECK(!source.request_cancellation())) {
        return false;
    }
    if (!BSKY_TEST_CHECK(moved.request_cancellation())) {
        return false;
    }
    return BSKY_TEST_CHECK(token.is_cancellation_requested());
}

bool cancellation_concurrent_requesters() {
    bsky_cpp::CancellationSource source;
    constexpr int requester_count = 8;
    std::atomic<int> first_requests{0};
    std::vector<std::thread> requesters;
    requesters.reserve(requester_count);
    for (int index = 0; index < requester_count; ++index) {
        requesters.emplace_back([&source, &first_requests]() {
            if (source.request_cancellation()) {
                first_requests.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (std::thread& requester : requesters) {
        requester.join();
    }
    if (!BSKY_TEST_CHECK(first_requests.load(std::memory_order_relaxed) == 1)) {
        return false;
    }
    return BSKY_TEST_CHECK(source.is_cancellation_requested());
}

bool cancellation_observation_race() {
    bsky_cpp::CancellationSource source;
    const bsky_cpp::CancellationToken token = source.token();
    std::mutex mutex;
    std::condition_variable condition;
    bool observer_ready = false;
    bool start_observation = false;
    bool initial_observation = false;
    std::atomic<bool> observed{false};
    std::atomic<bool> stop_observation{false};
    std::thread observer([&token, &observed, &stop_observation, &mutex, &condition,
                          &observer_ready, &start_observation, &initial_observation]() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            observer_ready = true;
        }
        condition.notify_one();
        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [&start_observation]() { return start_observation; });
        }
        while (!stop_observation.load(std::memory_order_acquire)) {
            if (token.is_cancellation_requested()) {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    observed.store(true, std::memory_order_release);
                }
                condition.notify_one();
                return;
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                initial_observation = true;
            }
            condition.notify_one();
            std::this_thread::yield();
        }
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(
                lock, std::chrono::seconds(1), [&observer_ready]() { return observer_ready; })) {
            stop_observation.store(true, std::memory_order_release);
            start_observation = true;
            lock.unlock();
            condition.notify_one();
            observer.join();
            return BSKY_TEST_CHECK(false);
        }
        start_observation = true;
    }
    condition.notify_one();

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(1),
                               [&initial_observation]() { return initial_observation; })) {
            stop_observation.store(true, std::memory_order_release);
            lock.unlock();
            observer.join();
            return BSKY_TEST_CHECK(false);
        }
    }

    if (!BSKY_TEST_CHECK(source.request_cancellation())) {
        stop_observation.store(true, std::memory_order_release);
        observer.join();
        return false;
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, std::chrono::seconds(1), [&observed]() {
                return observed.load(std::memory_order_acquire);
            })) {
            stop_observation.store(true, std::memory_order_release);
            lock.unlock();
            observer.join();
            return BSKY_TEST_CHECK(false);
        }
    }
    stop_observation.store(true, std::memory_order_release);
    observer.join();
    return BSKY_TEST_CHECK(observed.load(std::memory_order_acquire));
}

}  // namespace

int main() {
    if (!result_success_and_error() || !result_invalid_access() ||
        !result_exception_safety() || !cancellation_owner_and_repeat() ||
        !cancellation_moved_from_source() || !cancellation_concurrent_requesters() ||
        !cancellation_observation_race()) {
        return 1;
    }
    return 0;
}
