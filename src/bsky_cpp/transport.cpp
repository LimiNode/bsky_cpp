#include "bsky_cpp/transport.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <utility>

namespace bsky_cpp {
namespace {

Error invalid(std::string message, std::string operation) {
    return Error(ErrorCategory::InvalidArgument, std::move(message), std::move(operation));
}

Error transport_error(std::string message) {
    return Error(ErrorCategory::Transport, std::move(message), "transport.send");
}

std::string lowercase(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        result.push_back(static_cast<char>(std::tolower(character)));
    }
    return result;
}

bool has_control(std::string_view value) noexcept {
    for (const unsigned char character : value) {
        if (character < 0x20U || character == 0x7fU) {
            return true;
        }
    }
    return false;
}

bool valid_header_name(std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    for (const unsigned char character : value) {
        const bool token = std::isalnum(character) != 0 || character == '!' ||
                           character == '#' || character == '$' || character == '%' ||
                           character == '&' || character == '\'' || character == '*' ||
                           character == '+' || character == '-' || character == '.' ||
                           character == '^' || character == '_' || character == '`' ||
                           character == '|' || character == '~';
        if (!token) {
            return false;
        }
    }
    return true;
}

bool valid_method(HttpMethod method) noexcept {
    switch (method) {
        case HttpMethod::Get:
        case HttpMethod::Post:
        case HttpMethod::Put:
        case HttpMethod::Patch:
        case HttpMethod::Delete:
            return true;
    }
    return false;
}

bool valid_url(std::string_view value) noexcept {
    if (value.empty() || has_control(value)) {
        return false;
    }
    const bool http = value.rfind("http://", 0U) == 0U;
    const bool https = value.rfind("https://", 0U) == 0U;
    if (!http && !https) {
        return false;
    }
    const std::size_t authority_start = value.find("//") + 2U;
    const std::size_t authority_end = value.find_first_of("/?#", authority_start);
    const std::string_view authority = value.substr(
        authority_start, authority_end == std::string_view::npos
                              ? std::string_view::npos
                              : authority_end - authority_start);
    if (authority.empty() || value.find('@', authority_start) != std::string_view::npos) {
        return false;
    }
    for (const unsigned char character : value) {
        if (std::isspace(character) != 0) {
            return false;
        }
    }
    return true;
}

Result<void> set_header_impl(std::vector<TransportHeader>& headers, std::string name,
                             std::string value, std::string operation) {
    if (!valid_header_name(name) || has_control(value)) {
        return Result<void>::failure(
            invalid("invalid transport header", std::move(operation)));
    }
    const std::string canonical = lowercase(name);
    const auto existing = std::find_if(
        headers.begin(), headers.end(), [&canonical](const TransportHeader& header) {
            return header.name == canonical;
        });
    if (existing != headers.end()) {
        existing->value = std::move(value);
    } else {
        headers.push_back(TransportHeader{canonical, std::move(value)});
    }
    return Result<void>::success();
}

std::optional<std::string> find_header(const std::vector<TransportHeader>& headers,
                                       std::string_view name) {
    const std::string canonical = lowercase(name);
    const auto found = std::find_if(
        headers.begin(), headers.end(), [&canonical](const TransportHeader& header) {
            return header.name == canonical;
        });
    if (found == headers.end()) {
        return std::nullopt;
    }
    return found->value;
}

}  // namespace

const char* http_method_name(HttpMethod method) noexcept {
    switch (method) {
        case HttpMethod::Get:
            return "GET";
        case HttpMethod::Post:
            return "POST";
        case HttpMethod::Put:
            return "PUT";
        case HttpMethod::Patch:
            return "PATCH";
        case HttpMethod::Delete:
            return "DELETE";
    }
    return "GET";
}

Result<TransportRequest> TransportRequest::create(HttpMethod method, std::string url) {
    if (!valid_method(method)) {
        return Result<TransportRequest>::failure(
            invalid("unsupported transport method", "transport.request"));
    }
    if (!valid_url(url)) {
        return Result<TransportRequest>::failure(
            invalid("transport URL must be an absolute HTTP(S) URL", "transport.request"));
    }
    return Result<TransportRequest>::success(
        TransportRequest(method, std::move(url)));
}

Result<void> TransportRequest::set_header(std::string name, std::string value) {
    return set_header_impl(m_headers, std::move(name), std::move(value), "transport.request");
}

std::optional<std::string> TransportRequest::header(std::string_view name) const {
    return find_header(m_headers, name);
}

Result<TransportResponse> TransportResponse::create(std::uint16_t status) {
    if (status < 100U || status > 599U) {
        return Result<TransportResponse>::failure(
            invalid("transport status must be between 100 and 599", "transport.response"));
    }
    return Result<TransportResponse>::success(TransportResponse(status));
}

Result<void> TransportResponse::set_header(std::string name, std::string value) {
    return set_header_impl(m_headers, std::move(name), std::move(value), "transport.response");
}

std::optional<std::string> TransportResponse::header(std::string_view name) const {
    return find_header(m_headers, name);
}

FakeTransport::FakeTransport(Handler handler) : m_handler(std::move(handler)) {}

Result<TransportResponse> FakeTransport::send(const TransportRequest& request,
                                               CancellationToken cancellation) {
    if (cancellation.is_cancellation_requested()) {
        return Result<TransportResponse>::failure(
            Error(ErrorCategory::Cancelled, "transport request cancelled", "transport.send"));
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_requests.push_back(request);
    }
    if (!m_handler) {
        return Result<TransportResponse>::failure(
            transport_error("fake transport has no handler"));
    }
    return m_handler(request, cancellation);
}

std::vector<TransportRequest> FakeTransport::requests() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_requests;
}

void FakeTransport::clear_requests() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_requests.clear();
}

}  // namespace bsky_cpp
