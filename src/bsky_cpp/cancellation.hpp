#pragma once

#include <atomic>
#include <memory>

namespace bsky_cpp {
namespace detail {

struct CancellationState {
    std::atomic<bool> requested{false};
};

}  // namespace detail

/// A copyable observation handle for cooperative cancellation.
class CancellationToken {
public:
    /// Constructs a token that can never be cancelled.
    CancellationToken() noexcept = default;

    /// Returns whether a source has requested cancellation.
    bool is_cancellation_requested() const noexcept;

private:
    friend class CancellationSource;
    explicit CancellationToken(std::shared_ptr<detail::CancellationState> state) noexcept;

    std::shared_ptr<detail::CancellationState> m_state;
};

/// Owns the request side of a cooperative cancellation state.
class CancellationSource {
public:
    /// Creates an active cancellation source.
    CancellationSource();

    CancellationSource(const CancellationSource&) = delete;
    CancellationSource& operator=(const CancellationSource&) = delete;
    CancellationSource(CancellationSource&&) noexcept = default;
    CancellationSource& operator=(CancellationSource&&) noexcept = default;

    /// Creates an observation token that survives source destruction.
    CancellationToken token() const noexcept;

    /// Requests cancellation; returns true only for the first request.
    bool request_cancellation() noexcept;

    /// Returns whether cancellation has been requested.
    bool is_cancellation_requested() const noexcept;

private:
    std::shared_ptr<detail::CancellationState> m_state;
};

}  // namespace bsky_cpp
