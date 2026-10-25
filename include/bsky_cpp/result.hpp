#pragma once

#include "bsky_cpp/error.hpp"

#include <stdexcept>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace bsky_cpp {

/// Holds either a successful value or one typed Error.
///
/// \thread_safety Not thread-safe; synchronize access to a shared instance.
template <typename T>
class Result {
    static_assert(!std::is_void<T>::value, "Use Result<void> for a void result.");

public:
    /// Constructs a successful result, including move-only values.
    explicit Result(T value) : m_state(std::move(value)) {}

    /// Constructs a failed result.
    explicit Result(Error error) : m_state(std::move(error)) {}

    /// Returns true when the result contains a value.
    bool has_value() const noexcept {
        return std::holds_alternative<T>(m_state);
    }

    /// Returns true when the result contains a value.
    explicit operator bool() const noexcept {
        return has_value();
    }

    /// Returns the successful value or throws logic_error if failed.
    T& value() & {
        ensure_value();
        return std::get<T>(m_state);
    }

    /// Returns the successful value or throws logic_error if failed.
    const T& value() const& {
        ensure_value();
        return std::get<T>(m_state);
    }

    /// Moves the successful value or throws logic_error if failed.
    T&& value() && {
        ensure_value();
        return std::get<T>(std::move(m_state));
    }

    /// Returns the failure or throws logic_error if successful.
    Error& error() & {
        ensure_error();
        return std::get<Error>(m_state);
    }

    /// Returns the failure or throws logic_error if successful.
    const Error& error() const& {
        ensure_error();
        return std::get<Error>(m_state);
    }

    /// Creates a successful result without relying on implicit conversions.
    static Result success(T value) {
        return Result(std::move(value));
    }

    /// Creates a failed result without relying on implicit conversions.
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
    explicit Result(Error error) : m_error(std::move(error)) {}

    /// Returns true when the result is successful.
    bool has_value() const noexcept {
        return !m_error.has_value();
    }

    /// Returns true when the result is successful.
    explicit operator bool() const noexcept {
        return has_value();
    }

    /// Returns the failure or throws logic_error if successful.
    Error& error() {
        ensure_error();
        return *m_error;
    }

    /// Returns the failure or throws logic_error if successful.
    const Error& error() const {
        ensure_error();
        return *m_error;
    }

    /// Creates a successful void result.
    static Result success() noexcept {
        return Result();
    }

    /// Creates a failed void result.
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
