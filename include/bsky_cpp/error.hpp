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

/// A diagnostic error value with a deny-by-default metadata redaction policy.
class Error {
public:
    /// A key/value diagnostic entry. Sensitive keys are redacted on insertion.
    struct Metadata {
        std::string key;
        std::string value;
    };

    /// Constructs an error with a category, safe message, and optional operation.
    ///
    /// `message` and `operation` are trusted library-authored text. Untrusted
    /// server payloads belong in metadata, where keys are allowlisted and
    /// values are bounded/redacted.
    Error(ErrorCategory category, std::string message, std::string operation = {});

    /// Returns the category.
    ErrorCategory category() const noexcept;

    /// Returns the caller-provided safe message.
    const std::string& message() const noexcept;

    /// Returns the operation name, if supplied.
    const std::string& operation() const noexcept;

    /// Adds metadata through an exact-key, field-aware allowlist. Unknown keys,
    /// invalid values, and values that exceed the bounded metadata budget are
    /// redacted or rejected; this is not a universal sanitizer for arbitrary
    /// text.
    void add_metadata(std::string key, std::string value);

    /// Returns redacted diagnostic metadata.
    const std::vector<Metadata>& metadata() const noexcept;

private:
    static bool is_allowed_key(std::string_view key) noexcept;
    static bool is_valid_value(std::string_view key, std::string_view value) noexcept;
    std::size_t metadata_bytes() const noexcept;

    ErrorCategory m_category;
    std::string m_message;
    std::string m_operation;
    std::vector<Metadata> m_metadata;
};

}  // namespace bsky_cpp
