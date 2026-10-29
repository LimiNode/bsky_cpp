#include "bsky_cpp/transport.hpp"

#include "test_support.hpp"

#include <string>
#include <utility>
#include <vector>

namespace {

using bsky_cpp::CancellationSource;
using bsky_cpp::ErrorCategory;
using bsky_cpp::FakeTransport;
using bsky_cpp::HttpMethod;
using bsky_cpp::TransportRequest;
using bsky_cpp::TransportResponse;

int request_validation() {
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "not-a-url"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(static_cast<HttpMethod>(255U),
                                                   "https://example.com"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://?query"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://example.com/path here"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://user:secret@example.com"))) return 1;

    auto request = TransportRequest::create(HttpMethod::Post, "https://pds.example/xrpc/com.example.echo");
    if (!BSKY_TEST_CHECK(request.has_value())) return 1;
    if (!BSKY_TEST_CHECK(request.value().set_header("Content-Type", "application/json").has_value())) return 1;
    if (!BSKY_TEST_CHECK(request.value().set_header("content-type", "application/cbor").has_value())) return 1;
    if (!BSKY_TEST_CHECK(request.value().header("CONTENT-TYPE") ==
                         std::optional<std::string>("application/cbor"))) return 1;
    if (!BSKY_TEST_CHECK(!request.value().set_header("Bad Header", "value"))) return 1;
    request.value().set_body(std::vector<std::uint8_t>{1U, 2U, 3U});
    if (!BSKY_TEST_CHECK(request.value().body().size() == 3U)) return 1;
    return 0;
}

int fake_transport_behavior() {
    FakeTransport transport([](const TransportRequest& request,
                               bsky_cpp::CancellationToken cancellation) {
        if (cancellation.is_cancellation_requested()) {
            return bsky_cpp::Result<TransportResponse>::failure(
                bsky_cpp::Error(ErrorCategory::Cancelled, "cancelled", "test"));
        }
        auto response = TransportResponse::create(200U);
        if (!response) return response;
        response.value().set_header("Content-Type", "application/json");
        response.value().set_body(std::vector<std::uint8_t>{
            static_cast<std::uint8_t>(request.body().size())});
        return response;
    });

    auto request = TransportRequest::create(HttpMethod::Post, "https://pds.example/xrpc/test");
    if (!BSKY_TEST_CHECK(request.has_value())) return 1;
    request.value().set_body(std::vector<std::uint8_t>{9U, 8U});
    auto response = transport.send(request.value(), CancellationSource().token());
    if (!BSKY_TEST_CHECK(response && response.value().status() == 200U)) return 1;
    if (!BSKY_TEST_CHECK(response.value().body() == std::vector<std::uint8_t>{2U})) return 1;
    if (!BSKY_TEST_CHECK(transport.requests().size() == 1U)) return 1;

    CancellationSource source;
    source.request_cancellation();
    auto cancelled = transport.send(request.value(), source.token());
    if (!BSKY_TEST_CHECK(!cancelled &&
                         cancelled.error().category() == ErrorCategory::Cancelled)) return 1;
    if (!BSKY_TEST_CHECK(transport.requests().size() == 1U)) return 1;

    transport.clear_requests();
    if (!BSKY_TEST_CHECK(transport.requests().empty())) return 1;
    return 0;
}

}  // namespace

int main() {
    if (request_validation() != 0) return 1;
    return fake_transport_behavior();
}
