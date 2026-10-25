#include "bsky_cpp/cancellation.hpp"
#include "bsky_cpp/error.hpp"
#include "bsky_cpp/result.hpp"

#include <atomic>
#include <cassert>
#include <memory>
#include <string>
#include <thread>
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

void result_success_and_error() {
    bsky_cpp::Result<MoveOnly> success(MoveOnly(7));
    assert(success.has_value());
    assert(*success.value().value == 7);

    bsky_cpp::Error error(
        bsky_cpp::ErrorCategory::Transport, "request failed", "read_profile");
    error.add_metadata("status", "503");
    error.add_metadata("Authorization", "Bearer secret");
    bsky_cpp::Result<MoveOnly> failure(std::move(error));
    assert(!failure.has_value());
    assert(failure.error().category() == bsky_cpp::ErrorCategory::Transport);
    assert(failure.error().metadata().size() == 2);
    assert(failure.error().metadata()[1].value == "<redacted>");

    bsky_cpp::Result<void> void_success = bsky_cpp::Result<void>::success();
    assert(void_success.has_value());
    bsky_cpp::Result<void> void_failure = bsky_cpp::Result<void>::failure(
        bsky_cpp::Error(bsky_cpp::ErrorCategory::Cancelled, "cancelled"));
    assert(!void_failure.has_value());
    assert(void_failure.error().category() == bsky_cpp::ErrorCategory::Cancelled);
}

void cancellation_owner_and_repeat() {
    bsky_cpp::CancellationToken token;
    {
        bsky_cpp::CancellationSource source;
        token = source.token();
        assert(!token.is_cancellation_requested());
        assert(source.request_cancellation());
        assert(!source.request_cancellation());
        assert(source.is_cancellation_requested());
    }
    assert(token.is_cancellation_requested());
}

void cancellation_concurrent_requesters() {
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
    assert(first_requests.load(std::memory_order_relaxed) == 1);
    assert(source.is_cancellation_requested());
}

void cancellation_observation_race() {
    bsky_cpp::CancellationSource source;
    const bsky_cpp::CancellationToken token = source.token();
    std::atomic<bool> observed{false};
    std::thread observer([&token, &observed]() {
        while (!token.is_cancellation_requested()) {
        }
        observed.store(true, std::memory_order_release);
    });
    source.request_cancellation();
    observer.join();
    assert(observed.load(std::memory_order_acquire));
}

}  // namespace

int main() {
    result_success_and_error();
    cancellation_owner_and_repeat();
    cancellation_concurrent_requesters();
    cancellation_observation_race();
    return 0;
}
