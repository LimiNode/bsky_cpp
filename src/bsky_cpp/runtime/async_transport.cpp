#include "bsky_cpp/runtime/async_transport.hpp"

#include <exception>
#include <utility>

namespace bsky_cpp {
namespace {

Error transport_error(std::string message) {
    return Error(ErrorCategory::Transport, std::move(message), "transport.async_start");
}

}  // namespace

FakeAsyncTransport::FakeAsyncTransport(Handler handler)
    : m_handler(std::move(handler)) {}

Result<void> FakeAsyncTransport::start(TransportRequest request,
                                       CancellationToken cancellation,
                                       Completion completion) {
    if (cancellation.is_cancellation_requested()) {
        return Result<void>::failure(
            Error(ErrorCategory::Cancelled,
                  "transport request cancelled before admission",
                  "transport.async_start"));
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_requests.push_back(request);
    }

    if (!m_handler) {
        return Result<void>::failure(transport_error("fake async transport has no handler"));
    }

    try {
        return m_handler(std::move(request), cancellation, std::move(completion));
    } catch (const std::exception&) {
        return Result<void>::failure(
            transport_error("fake async transport handler threw an exception"));
    } catch (...) {
        return Result<void>::failure(
            transport_error("fake async transport handler threw an unknown exception"));
    }
}

std::vector<TransportRequest> FakeAsyncTransport::requests() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_requests;
}

void FakeAsyncTransport::clear_requests() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_requests.clear();
}

}  // namespace bsky_cpp
