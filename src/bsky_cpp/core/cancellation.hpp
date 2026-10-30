/// \file cancellation.hpp
/// \brief Cooperative cancellation source and observation token.
#pragma once

#include <atomic>
#include <memory>

namespace bsky_cpp {
namespace detail {

/// Shared atomic state retained by cancellation tokens and their source.
struct CancellationState {
    /// Atomic cancellation flag shared by all observers.
    std::atomic<bool> requested{false};
};

}  // namespace detail

/// A copyable observation handle for cooperative cancellation.
class CancellationToken {
public:
    /// Constructs a token that can never be cancelled.
    CancellationToken() noexcept = default;

    /// Returns whether a source has requested cancellation.
    /// \return `true` when cancellation was requested.
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

    /// Cancellation sources are not copyable.
    CancellationSource(const CancellationSource&) = delete;
    /// Cancellation sources are not copy-assignable.
    CancellationSource& operator=(const CancellationSource&) = delete;
    /// Transfers ownership of a cancellation state.
    CancellationSource(CancellationSource&&) noexcept = default;
    /// Transfers ownership by move assignment.
    /// \return `*this` after assignment.
    CancellationSource& operator=(CancellationSource&&) noexcept = default;

    /// Creates an observation token that survives source destruction.
    /// \return A copyable observation token.
    CancellationToken token() const noexcept;

    /// Requests cancellation; returns true only for the first request.
    /// \return `true` when this call changed the state.
    bool request_cancellation() noexcept;

    /// Returns whether cancellation has been requested.
    /// \return `true` after a cancellation request.
    bool is_cancellation_requested() const noexcept;

private:
    std::shared_ptr<detail::CancellationState> m_state;
};

}  // namespace bsky_cpp
