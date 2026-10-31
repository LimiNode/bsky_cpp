#include "bsky_cpp/core/cancellation.hpp"

#include <utility>

namespace bsky_cpp {

CancellationToken::CancellationToken(
    std::shared_ptr<detail::CancellationState> state) noexcept
    : m_state(std::move(state)) {}

bool CancellationToken::is_cancellation_requested() const noexcept {
    return m_state != nullptr && m_state->requested.load(std::memory_order_acquire);
}

CancellationSource::CancellationSource()
    : m_state(std::make_shared<detail::CancellationState>()) {}

CancellationToken CancellationSource::token() const noexcept {
    return CancellationToken(m_state);
}

bool CancellationSource::request_cancellation() noexcept {
    if (m_state == nullptr) {
        return false;
    }
    bool expected = false;
    return m_state->requested.compare_exchange_strong(
        expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
}

bool CancellationSource::is_cancellation_requested() const noexcept {
    return m_state != nullptr && m_state->requested.load(std::memory_order_acquire);
}

}  // namespace bsky_cpp
