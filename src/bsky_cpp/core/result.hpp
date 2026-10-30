/// \file result.hpp
/// \brief Value-or-error result containers for the public SDK API.
#pragma once

#include "bsky_cpp/core/bsky_error.hpp"

#include <stdexcept>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace bsky_cpp {

/// Holds either a successful value or one typed Error.
///
/// The alternative selected by the result is stable after construction;
/// contained values are intentionally mutable through the non-const accessors.
/// Assignment is intentionally deleted so the public API cannot expose
/// `std::variant::valueless_by_exception`.
/// \par Thread safety
/// Not thread-safe; synchronize access to a shared instance.
/// \tparam T Successful value type.
template <typename T>
class Result {
    static_assert(!std::is_void<T>::value, "Use Result<void> for a void result.");

public:
    /// Constructs a successful result, including move-only values.
    /// \param value Successful value.
    explicit Result(T value) : m_state(std::move(value)) {}

    /// Constructs a failed result.
    /// \param error Typed failure.
    explicit Result(Error error) : m_state(std::move(error)) {}

    /// Copies a result and its contained value or error.
    Result(const Result&) = default;
    /// Moves a result and its contained value or error.
    Result(Result&&) = default;
    /// Assignment is intentionally disabled for value results.
    Result& operator=(const Result&) = delete;
    /// Assignment is intentionally disabled for value results.
    Result& operator=(Result&&) = delete;

    /// Returns true when the result contains a value.
    /// \return `true` when successful.
    bool has_value() const noexcept {
        return std::holds_alternative<T>(m_state);
    }

    /// Returns true when the result contains a value.
    /// \return `true` when successful.
    explicit operator bool() const noexcept {
        return has_value();
    }

    /// Returns the successful value or throws logic_error if failed.
    /// \return The mutable value.
    T& value() & {
        ensure_value();
        return std::get<T>(m_state);
    }

    /// Returns the successful value or throws logic_error if failed.
    /// \return The const value.
    const T& value() const& {
        ensure_value();
        return std::get<T>(m_state);
    }

    /// Moves the successful value or throws logic_error if failed.
    /// \return The moved value.
    T&& value() && {
        ensure_value();
        return std::get<T>(std::move(m_state));
    }

    /// Returns the failure or throws logic_error if successful.
    /// \return The mutable error.
    Error& error() & {
        ensure_error();
        return std::get<Error>(m_state);
    }

    /// Returns the failure or throws logic_error if successful.
    /// \return The const error.
    const Error& error() const& {
        ensure_error();
        return std::get<Error>(m_state);
    }

    /// Creates a successful result without relying on implicit conversions.
    /// \param value Successful value.
    /// \return A successful result.
    static Result success(T value) {
        return Result(std::move(value));
    }

    /// Creates a failed result without relying on implicit conversions.
    /// \param error Typed failure.
    /// \return A failed result.
    static Result failure(Error error) {
        return Result(std::move(error));
    }

private:
    void ensure_value() const {
        if (!has_value()) {
            throw std::logic_error("bsky_cpp::Result does not contain a value");
        }
    }

    void ensure_error() const {
        if (has_value()) {
            throw std::logic_error("bsky_cpp::Result does not contain an error");
        }
    }

    std::variant<T, Error> m_state;
};

/// Represents success or one typed Error without a value payload.
template <>
class Result<void> {
public:
    /// Constructs a successful void result.
    Result() noexcept = default;

    /// Constructs a failed void result.
    /// \param error Typed failure.
    explicit Result(Error error) : m_error(std::move(error)) {}

    /// Copies a void result and its optional error.
    Result(const Result&) = default;
    /// Moves a void result and its optional error.
    Result(Result&&) noexcept = default;
    /// Copy-assigns a void result.
    /// \return `*this` after assignment.
    Result& operator=(const Result&) = default;
    /// Move-assigns a void result.
    /// \return `*this` after assignment.
    Result& operator=(Result&&) noexcept = default;

    /// Returns true when the result is successful.
    /// \return `true` when successful.
    bool has_value() const noexcept {
        return !m_error.has_value();
    }

    /// Returns true when the result is successful.
    /// \return `true` when successful.
    explicit operator bool() const noexcept {
        return has_value();
    }

    /// Returns the failure or throws logic_error if successful.
    /// \return The mutable error.
    Error& error() {
        ensure_error();
        return *m_error;
    }

    /// Returns the failure or throws logic_error if successful.
    /// \return The const error.
    const Error& error() const {
        ensure_error();
        return *m_error;
    }

    /// Creates a successful void result.
    /// \return A successful result.
    static Result success() noexcept {
        return Result();
    }

    /// Creates a failed void result.
    /// \param error Typed failure.
    /// \return A failed result.
    static Result failure(Error error) {
        return Result(std::move(error));
    }

private:
    void ensure_error() const {
        if (!m_error.has_value()) {
            throw std::logic_error("bsky_cpp::Result<void> does not contain an error");
        }
    }

    std::optional<Error> m_error;
};

}  // namespace bsky_cpp
