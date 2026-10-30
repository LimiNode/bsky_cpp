#include "bsky_cpp/transport.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <limits>
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

constexpr std::size_t kMaxHeaders = 128U;
constexpr std::size_t kMaxHeaderNameBytes = 256U;
constexpr std::size_t kMaxHeaderValueBytes = 16U * 1024U;
constexpr std::size_t kMaxHeaderBytes = 64U * 1024U;

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

bool is_token_character(unsigned char character) noexcept {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '!' ||
           character == '#' || character == '$' || character == '%' ||
           character == '&' || character == '\'' || character == '*' ||
           character == '+' || character == '-' || character == '.' ||
           character == '^' || character == '_' || character == '`' ||
           character == '|' || character == '~';
}

bool valid_header_name(std::string_view value) noexcept {
    if (value.empty() || value.size() > kMaxHeaderNameBytes) {
        return false;
    }
    for (const unsigned char character : value) {
        if (!is_token_character(character)) {
            return false;
        }
    }
    return true;
}

bool valid_percent_escapes(std::string_view value) noexcept {
    constexpr auto is_hex = [](unsigned char character) noexcept {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f') ||
               (character >= 'A' && character <= 'F');
    };
    for (std::size_t index = 0U; index < value.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (character == '%') {
            if (index + 2U >= value.size() || !is_hex(value[index + 1U]) ||
                !is_hex(value[index + 2U])) {
                return false;
            }
            index += 2U;
        } else if (character < 0x20U || character == 0x7fU || character >= 0x80U ||
                   std::isspace(character) != 0) {
            return false;
        }
    }
    return true;
}

bool valid_dns_host(std::string_view host) noexcept {
    if (host.empty() || host.front() == '.' || host.back() == '.') {
        return false;
    }
    bool all_numeric = true;
    std::size_t label_count = 0U;
    std::size_t label_start = 0U;
    while (label_start < host.size()) {
        const std::size_t label_end = host.find('.', label_start);
        const std::size_t end = label_end == std::string_view::npos ? host.size() : label_end;
        if (end == label_start || host[label_start] == '-' || host[end - 1U] == '-') {
            return false;
        }
        for (std::size_t index = label_start; index < end; ++index) {
            const unsigned char character = static_cast<unsigned char>(host[index]);
            if (character < '0' || character > '9') {
                all_numeric = false;
            }
            if (!((character >= 'A' && character <= 'Z') ||
                  (character >= 'a' && character <= 'z') ||
                  (character >= '0' && character <= '9') || character == '-')) {
                return false;
            }
        }
        ++label_count;
        if (label_end == std::string_view::npos) {
            break;
        }
        label_start = label_end + 1U;
    }
    if (all_numeric && label_count == 4U) {
        label_start = 0U;
        while (label_start < host.size()) {
            const std::size_t label_end = host.find('.', label_start);
            const std::size_t end = label_end == std::string_view::npos ? host.size() : label_end;
            unsigned int value = 0U;
            for (std::size_t index = label_start; index < end; ++index) {
                value = value * 10U + static_cast<unsigned int>(host[index] - '0');
            }
            if (value > 255U) {
                return false;
            }
            if (label_end == std::string_view::npos) {
                break;
            }
            label_start = label_end + 1U;
        }
    }
    return true;
}

bool valid_ipv6_literal(std::string_view host) noexcept {
    const std::size_t zone = host.find('%');
    const std::string_view address = host.substr(0U, zone);
    if (address.empty()) {
        return false;
    }
    if (zone != std::string_view::npos &&
        (zone + 3U > host.size() || host.substr(zone, 3U) != "%25")) {
        return false;
    }
    const bool compressed = address.find("::") != std::string_view::npos;
    if (compressed && address.find("::", address.find("::") + 2U) !=
                          std::string_view::npos) {
        return false;
    }
    const std::size_t compression = address.find("::");
    const std::string_view left = compressed ? address.substr(0U, compression) : address;
    const std::string_view right = compressed ? address.substr(compression + 2U) : "";
    auto count_groups = [](std::string_view part) noexcept -> int {
        if (part.empty()) {
            return 0;
        }
        int groups = 0;
        std::size_t start = 0U;
        while (start < part.size()) {
            const std::size_t end = part.find(':', start);
            const std::size_t stop = end == std::string_view::npos ? part.size() : end;
            if (stop == start || stop - start > 4U) {
                return -1;
            }
            for (std::size_t index = start; index < stop; ++index) {
                const unsigned char character = static_cast<unsigned char>(part[index]);
                if (!((character >= 'A' && character <= 'F') ||
                      (character >= 'a' && character <= 'f') ||
                      (character >= '0' && character <= '9'))) {
                    return -1;
                }
            }
            ++groups;
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1U;
        }
        return groups;
    };
    const int groups = count_groups(left) + count_groups(right);
    if (groups < 0 || (compressed ? groups >= 8 : groups != 8)) {
        return false;
    }
    if (address.find(':') == std::string_view::npos) {
            return false;
    }
    return host.find(':') != std::string_view::npos;
}

bool valid_port(std::string_view port) noexcept {
    if (port.empty() || port.size() > 5U) {
        return false;
    }
    std::uint32_t value = 0U;
    for (const unsigned char character : port) {
        if (character < '0' || character > '9') {
            return false;
        }
        value = value * 10U + static_cast<std::uint32_t>(character - '0');
    }
    return value <= 65535U;
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
    if (authority.empty() || authority.find('@') != std::string_view::npos ||
        value.find('#') != std::string_view::npos) {
        return false;
    }
    for (const unsigned char character : value) {
        if (std::isspace(character) != 0) {
            return false;
        }
    }
    if (!valid_percent_escapes(value)) {
        return false;
    }
    if (authority.front() == '[') {
        const std::size_t closing = authority.find(']');
        if (closing == std::string_view::npos ||
            !valid_ipv6_literal(authority.substr(1U, closing - 1U))) {
            return false;
        }
        if (closing + 1U < authority.size() &&
            (authority[closing + 1U] != ':' ||
             !valid_port(authority.substr(closing + 2U)))) {
            return false;
        }
    } else {
        const std::size_t colon = authority.find(':');
        const std::string_view host = authority.substr(0U, colon);
        if (!valid_dns_host(host)) {
            return false;
        }
        if (colon != std::string_view::npos && !valid_port(authority.substr(colon + 1U))) {
            return false;
        }
    }
    return true;
}

std::size_t header_bytes(const std::vector<TransportHeader>& headers) noexcept {
    std::size_t total = 0U;
    for (const TransportHeader& header : headers) {
        if (total > std::numeric_limits<std::size_t>::max() - header.name.size() -
                              header.value.size()) {
            return std::numeric_limits<std::size_t>::max();
        }
        total += header.name.size() + header.value.size();
    }
    return total;
}

Result<void> append_header_impl(std::vector<TransportHeader>& headers, std::string name,
                                std::string value, std::string operation) {
    if (!valid_header_name(name) || value.size() > kMaxHeaderValueBytes || has_control(value)) {
        return Result<void>::failure(
            invalid("invalid transport header", std::move(operation)));
    }
    if (headers.size() >= kMaxHeaders ||
        header_bytes(headers) > kMaxHeaderBytes - name.size() - value.size()) {
        return Result<void>::failure(
            invalid("transport header limits exceeded", std::move(operation)));
    }
    const std::string canonical = lowercase(name);
    headers.push_back(TransportHeader{canonical, std::move(value)});
    return Result<void>::success();
}

Result<void> set_header_impl(std::vector<TransportHeader>& headers, std::string name,
                             std::string value, std::string operation) {
    if (!valid_header_name(name) || value.size() > kMaxHeaderValueBytes || has_control(value)) {
        return Result<void>::failure(
            invalid("invalid transport header", std::move(operation)));
    }
    const std::string canonical = lowercase(name);
    std::vector<TransportHeader> replacement;
    replacement.reserve(headers.size() + 1U);
    for (TransportHeader& header : headers) {
        if (header.name != canonical) {
            replacement.push_back(std::move(header));
        }
    }
    if (replacement.size() >= kMaxHeaders ||
        header_bytes(replacement) > kMaxHeaderBytes - canonical.size() - value.size()) {
        return Result<void>::failure(
            invalid("transport header limits exceeded", std::move(operation)));
    }
    replacement.push_back(TransportHeader{canonical, std::move(value)});
    headers = std::move(replacement);
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

std::vector<std::string> find_headers(const std::vector<TransportHeader>& headers,
                                      std::string_view name) {
    const std::string canonical = lowercase(name);
    std::vector<std::string> values;
    for (const TransportHeader& header : headers) {
        if (header.name == canonical) {
            values.push_back(header.value);
        }
    }
    return values;
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

std::vector<std::string> TransportRequest::headers(std::string_view name) const {
    return find_headers(m_headers, name);
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

Result<void> TransportResponse::append_header(std::string name, std::string value) {
    return append_header_impl(m_headers, std::move(name), std::move(value),
                              "transport.response");
}

std::optional<std::string> TransportResponse::header(std::string_view name) const {
    return find_header(m_headers, name);
}

std::vector<std::string> TransportResponse::headers(std::string_view name) const {
    return find_headers(m_headers, name);
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
    try {
        return m_handler(request, cancellation);
    } catch (const std::exception&) {
        return Result<TransportResponse>::failure(
            transport_error("fake transport handler threw an exception"));
    } catch (...) {
        return Result<TransportResponse>::failure(
            transport_error("fake transport handler threw an unknown exception"));
    }
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
