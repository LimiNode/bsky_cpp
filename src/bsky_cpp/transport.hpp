#pragma once

#include "bsky_cpp/cancellation.hpp"
#include "bsky_cpp/result.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bsky_cpp {

/// HTTP method used by a transport request.
enum class HttpMethod : std::uint8_t { Get, Post, Put, Patch, Delete };

/// Returns the wire spelling of an HTTP method, or `UNKNOWN` for an invalid enum.
const char* http_method_name(HttpMethod method) noexcept;

/// A bounded, owned HTTP header entry.
struct TransportHeader {
    std::string name;
    std::string value;
};

/// An owned request value passed to a transport adapter.
class TransportRequest {
public:
    /// Creates a request after validating its method and absolute HTTP URL.
    static Result<TransportRequest> create(HttpMethod method, std::string url);

    HttpMethod method() const noexcept { return m_method; }
    const std::string& url() const noexcept { return m_url; }
    const std::vector<TransportHeader>& headers() const noexcept { return m_headers; }
    const std::vector<std::uint8_t>& body() const noexcept { return m_body; }

    /// Adds or replaces a case-insensitive header name.
    Result<void> set_header(std::string name, std::string value);

    /// Replaces the owned request body.
    void set_body(std::vector<std::uint8_t> body) noexcept { m_body = std::move(body); }

    /// Finds a header by case-insensitive name.
    std::optional<std::string> header(std::string_view name) const;

    /// Returns all values for a case-insensitive header name in wire order.
    std::vector<std::string> headers(std::string_view name) const;

private:
    TransportRequest(HttpMethod method, std::string url)
        : m_method(method), m_url(std::move(url)) {}

    HttpMethod m_method;
    std::string m_url;
    std::vector<TransportHeader> m_headers;
    std::vector<std::uint8_t> m_body;
};

/// An owned response value returned by a transport adapter.
class TransportResponse {
public:
    /// Creates a response with an HTTP status in the 100..599 range.
    static Result<TransportResponse> create(std::uint16_t status);

    std::uint16_t status() const noexcept { return m_status; }
    const std::vector<TransportHeader>& headers() const noexcept { return m_headers; }
    const std::vector<std::uint8_t>& body() const noexcept { return m_body; }

    /// Adds or replaces all response fields with a case-insensitive name.
    Result<void> set_header(std::string name, std::string value);

    /// Appends a repeated response header field without changing existing order.
    Result<void> append_header(std::string name, std::string value);

    /// Replaces the owned response body.
    void set_body(std::vector<std::uint8_t> body) noexcept { m_body = std::move(body); }

    /// Finds a header by case-insensitive name.
    std::optional<std::string> header(std::string_view name) const;

    /// Returns all values for a case-insensitive header name in wire order.
    std::vector<std::string> headers(std::string_view name) const;

private:
    explicit TransportResponse(std::uint16_t status) : m_status(status) {}

    std::uint16_t m_status;
    std::vector<TransportHeader> m_headers;
    std::vector<std::uint8_t> m_body;
};

/// Transport-neutral synchronous request boundary.
///
/// Implementations own their connection and buffering strategy. The request,
/// response, and body values are owned and remain valid independently of any
/// adapter buffers. This is a low-level blocking seam: M1c must invoke it from
/// a bounded worker bridge and never from an event or callback thread. It is
/// not a required implementation path for native-async adapters: a future
/// `kurlyk` adapter may deliver completion directly into the operation runtime
/// without `future.get()` or a per-request waiter thread. Native async adapter
/// integration and operation admission remain M1c responsibilities.
class Transport {
public:
    virtual ~Transport() = default;

    /// Sends one request, cooperatively observing cancellation.
    virtual Result<TransportResponse> send(const TransportRequest& request,
                                            CancellationToken cancellation) = 0;
};

/// Deterministic in-memory transport for unit and conformance tests.
class FakeTransport final : public Transport {
public:
    using Handler = std::function<Result<TransportResponse>(
        const TransportRequest&, CancellationToken)>;

    /// Creates a fake transport with an optional request handler.
    ///
    /// Request history is safe for concurrent access. The handler is invoked
    /// outside the history mutex and must be thread-safe when `send()` is
    /// called concurrently. Exceptions from the handler are contained and
    /// mapped to a generic transport error without exposing `what()` text.
    explicit FakeTransport(Handler handler = {});

    Result<TransportResponse> send(const TransportRequest& request,
                                   CancellationToken cancellation) override;

    /// Returns an owned snapshot of requests accepted by the fake.
    std::vector<TransportRequest> requests() const;

    /// Removes the recorded request history.
    void clear_requests();

private:
    Handler m_handler;
    std::vector<TransportRequest> m_requests;
    mutable std::mutex m_mutex;
};

}  // namespace bsky_cpp
