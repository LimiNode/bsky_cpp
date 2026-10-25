#include "bsky_cpp/error.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace bsky_cpp {
namespace {

bool contains_case_insensitive(std::string_view value, std::string_view needle) noexcept {
    if (needle.size() > value.size()) {
        return false;
    }
    return std::search(value.begin(), value.end(), needle.begin(), needle.end(),
                       [](char left, char right) {
                           const auto left_value = static_cast<unsigned char>(left);
                           const auto right_value = static_cast<unsigned char>(right);
                           return std::tolower(left_value) == std::tolower(right_value);
                       }) != value.end();
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
    if (is_sensitive_key(key)) {
        value = "<redacted>";
    }
    m_metadata.push_back(Metadata{std::move(key), std::move(value)});
}

const std::vector<Error::Metadata>& Error::metadata() const noexcept {
    return m_metadata;
}

bool Error::is_sensitive_key(std::string_view key) noexcept {
    return contains_case_insensitive(key, "authorization") ||
           contains_case_insensitive(key, "cookie") ||
           contains_case_insensitive(key, "password") ||
           contains_case_insensitive(key, "secret") ||
           contains_case_insensitive(key, "token") ||
           contains_case_insensitive(key, "credential");
}

}  // namespace bsky_cpp
