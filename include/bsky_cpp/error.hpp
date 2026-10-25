#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bsky_cpp {

/// Classifies a failure without exposing transport-specific exception types.
enum class ErrorCategory : std::uint8_t {
    InvalidArgument,
    Cancelled,
    TimedOut,
    Shutdown,
    Transport,
    Protocol,
    Decode,
    Authentication,
    Authorization,
    RateLimited,
    Conflict,
    OutcomeUnknown,
    Internal
};

/// Returns a stable diagnostic name for an error category.
const char* error_category_name(ErrorCategory category) noexcept;

/// A non-throwing, secret-redacting diagnostic error value.
class Error {
public:
    /// A key/value diagnostic entry. Sensitive keys are redacted on insertion.
    struct Metadata {
        std::string key;
        std::string value;
    };

    /// Constructs an error with a category, safe message, and optional operation.
    ///
    /// Callers must not place credentials in `message` or `operation`.
    Error(ErrorCategory category, std::string message, std::string operation = {});

    /// Returns the category.
    ErrorCategory category() const noexcept;

    /// Returns the caller-provided safe message.
    const std::string& message() const noexcept;

    /// Returns the operation name, if supplied.
    const std::string& operation() const noexcept;

    /// Adds metadata; values for secret-like keys are replaced with `<redacted>`.
    void add_metadata(std::string key, std::string value);

    /// Returns redacted diagnostic metadata.
    const std::vector<Metadata>& metadata() const noexcept;

private:
    static bool is_sensitive_key(std::string_view key) noexcept;

    ErrorCategory m_category;
    std::string m_message;
    std::string m_operation;
    std::vector<Metadata> m_metadata;
};

}  // namespace bsky_cpp
