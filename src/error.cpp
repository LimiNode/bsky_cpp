#include "bsky_cpp/error.hpp"

#include <array>
#include <cctype>
#include <limits>
#include <utility>

namespace bsky_cpp {
namespace {

constexpr std::size_t kMaxMetadataEntries = 16U;
constexpr std::size_t kMaxMetadataBytes = 4096U;
constexpr std::size_t kMaxMetadataKeyBytes = 64U;
constexpr std::size_t kMaxMetadataValueBytes = 256U;

bool is_decimal(std::string_view value, std::uint64_t maximum) noexcept {
    if (value.empty() || value.size() > 20U) {
        return false;
    }
    std::uint64_t parsed = 0U;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return false;
        }
        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (maximum - digit) / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
    }
    return parsed <= maximum;
}

bool is_safe_token(std::string_view value) noexcept {
    if (value.empty() || value.size() > kMaxMetadataValueBytes) {
        return false;
    }
    for (const unsigned char character : value) {
        if (!(std::isalnum(character) || character == '_' || character == '-' ||
              character == '.' || character == ':' || character == '/')) {
            return false;
        }
    }
    return true;
}

bool is_safe_uri(std::string_view value) noexcept {
    const bool has_supported_scheme = value.rfind("at://", 0U) == 0U ||
                                      value.rfind("https://", 0U) == 0U;
    if (!has_supported_scheme || value.size() > kMaxMetadataValueBytes) {
        return false;
    }
    for (const unsigned char character : value) {
        if (!(std::isalnum(character) || character == ':' || character == '/' ||
              character == '.' || character == '-' || character == '_' ||
              character == '~')) {
            return false;
        }
    }
    return value.find('@') == std::string_view::npos &&
           value.find('?') == std::string_view::npos &&
           value.find('#') == std::string_view::npos;
}

bool is_safe_host(std::string_view value) noexcept {
    if (value.empty() || value.size() > kMaxMetadataValueBytes) {
        return false;
    }
    for (const unsigned char character : value) {
        if (!(std::isalnum(character) || character == '.' || character == '-' ||
              character == ':' || character == '[' || character == ']')) {
            return false;
        }
    }
    return true;
}

}  // namespace

const char* error_category_name(ErrorCategory category) noexcept {
    switch (category) {
        case ErrorCategory::InvalidArgument:
            return "invalid_argument";
        case ErrorCategory::Cancelled:
            return "cancelled";
        case ErrorCategory::TimedOut:
            return "timed_out";
        case ErrorCategory::Shutdown:
            return "shutdown";
        case ErrorCategory::Transport:
            return "transport";
        case ErrorCategory::Protocol:
            return "protocol";
        case ErrorCategory::Decode:
            return "decode";
        case ErrorCategory::Authentication:
            return "authentication";
        case ErrorCategory::Authorization:
            return "authorization";
        case ErrorCategory::RateLimited:
            return "rate_limited";
        case ErrorCategory::Conflict:
            return "conflict";
        case ErrorCategory::OutcomeUnknown:
            return "outcome_unknown";
        case ErrorCategory::Internal:
            return "internal";
    }
    return "internal";
}

Error::Error(ErrorCategory category, std::string message, std::string operation)
    : m_category(category),
      m_message(std::move(message)),
      m_operation(std::move(operation)) {}

ErrorCategory Error::category() const noexcept {
    return m_category;
}

const std::string& Error::message() const noexcept {
    return m_message;
}

const std::string& Error::operation() const noexcept {
    return m_operation;
}

void Error::add_metadata(std::string key, std::string value) {
    if (m_metadata.size() >= kMaxMetadataEntries) {
        return;
    }

    const bool allowed = key.size() <= kMaxMetadataKeyBytes && is_allowed_key(key);
    if (!allowed) {
        key = "<redacted>";
        value = "<redacted>";
    } else if (!is_valid_value(key, value)) {
        value = "<redacted>";
    } else if (value.size() > kMaxMetadataValueBytes) {
        value.resize(kMaxMetadataValueBytes);
    }

    if (key.size() + value.size() > kMaxMetadataBytes ||
        metadata_bytes() + key.size() + value.size() > kMaxMetadataBytes) {
        return;
    }
    m_metadata.push_back(Metadata{std::move(key), std::move(value)});
}

const std::vector<Error::Metadata>& Error::metadata() const noexcept {
    return m_metadata;
}

bool Error::is_allowed_key(std::string_view key) noexcept {
    static constexpr std::array<std::string_view, 10U> allowed_keys = {
        "status",       "error_code",    "error_name", "retry_after_ms",
        "attempt",      "operation_id", "collection", "rkey",
        "cid",          "uri"};
    for (const std::string_view allowed : allowed_keys) {
        if (key == allowed) {
            return true;
        }
    }
    return key == "endpoint_host";
}

bool Error::is_valid_value(std::string_view key, std::string_view value) noexcept {
    if (key == "status") {
        return value.size() == 3U && value[0] >= '1' && value[0] <= '5' &&
               is_decimal(value, 599U);
    }
    if (key == "retry_after_ms") {
        return is_decimal(value, 86'400'000U);
    }
    if (key == "attempt") {
        return is_decimal(value, 1'000'000U);
    }
    if (key == "uri") {
        return is_safe_uri(value);
    }
    if (key == "endpoint_host") {
        return is_safe_host(value);
    }
    return is_safe_token(value);
}

std::size_t Error::metadata_bytes() const noexcept {
    std::size_t total = 0U;
    for (const Metadata& entry : m_metadata) {
        if (total > std::numeric_limits<std::size_t>::max() - entry.key.size() -
                              entry.value.size()) {
            return std::numeric_limits<std::size_t>::max();
        }
        total += entry.key.size() + entry.value.size();
    }
    return total;
}

}  // namespace bsky_cpp
