/// \file bsky_error.hpp
/// \brief Typed diagnostic errors with bounded metadata redaction.
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
/// \param category Error category.
/// \return Stable lowercase category name.
const char* error_category_name(ErrorCategory category) noexcept;

/// A diagnostic error value with a deny-by-default metadata redaction policy.
class Error {
public:
    /// A key/value diagnostic entry. Sensitive keys are redacted on insertion.
    struct Metadata {
        /// Allowlisted metadata key.
        std::string key;
        /// Redacted metadata value.
        std::string value;
    };

    /// Constructs an error with a category, safe message, and optional operation.
    ///
    /// `message` and `operation` are trusted library-authored text. Untrusted
    /// server payloads belong in metadata, where keys are allowlisted and
    /// values are bounded/redacted.
    /// \param category Typed failure category.
    /// \param message Safe diagnostic message.
    /// \param operation Optional operation identifier.
    Error(ErrorCategory category, std::string message, std::string operation = {});

    /// Returns the category.
    /// \return The typed error category.
    ErrorCategory category() const noexcept;

    /// Returns the caller-provided safe message.
    /// \return The diagnostic message.
    const std::string& message() const noexcept;

    /// Returns the operation name, if supplied.
    /// \return The operation identifier, possibly empty.
    const std::string& operation() const noexcept;

    /// Adds metadata through an exact-key, field-aware allowlist. Unknown keys,
    /// invalid values, and values that exceed the bounded metadata budget are
    /// redacted or rejected; this is not a universal sanitizer for arbitrary
    /// text.
    /// \param key Allowlisted metadata key.
    /// \param value Candidate metadata value.
    void add_metadata(std::string key, std::string value);

    /// Returns redacted diagnostic metadata.
    /// \return The metadata entries in insertion order.
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
