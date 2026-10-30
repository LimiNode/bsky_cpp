#include "bsky_cpp/transport.hpp"

#include "test_support.hpp"

#include <atomic>
#include <string>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

using bsky_cpp::CancellationSource;
using bsky_cpp::ErrorCategory;
using bsky_cpp::FakeTransport;
using bsky_cpp::HttpMethod;
using bsky_cpp::TransportRequest;
using bsky_cpp::TransportResponse;

bool same_headers(const std::vector<bsky_cpp::TransportHeader>& left,
                  const std::vector<bsky_cpp::TransportHeader>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (left[index].name != right[index].name || left[index].value != right[index].value) {
            return false;
        }
    }
    return true;
}

int request_validation() {
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "not-a-url"))) return 1;
    const auto unknown_method = static_cast<HttpMethod>(255U);
    if (!BSKY_TEST_CHECK(!TransportRequest::create(unknown_method, "https://example.com"))) return 1;
    if (!BSKY_TEST_CHECK(std::string(bsky_cpp::http_method_name(unknown_method)) == "UNKNOWN")) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://?query"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://example.com/path here"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://user:secret@example.com"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://:443/path"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://example.com:abc/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://example.com:65536/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://[::1/path"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://[:::]/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://999.999.999.999/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://001.2.3.4/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://example.com/%ZZ"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://example.com#fragment"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://example.com/path#fragment"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://example.com/?q=%"))) return 1;
    if (!BSKY_TEST_CHECK(TransportRequest::create(HttpMethod::Get,
                                                   "https://example.com/path@name")
                             .has_value())) return 1;
    if (!BSKY_TEST_CHECK(TransportRequest::create(HttpMethod::Get,
                                                   "https://example.com/?email=a@b")
                             .has_value())) return 1;
    if (!BSKY_TEST_CHECK(TransportRequest::create(HttpMethod::Get, "http://localhost:8080/")
                             .has_value())) return 1;
    if (!BSKY_TEST_CHECK(TransportRequest::create(HttpMethod::Get, "https://[::1]:443/path")
                             .has_value())) return 1;
    if (!BSKY_TEST_CHECK(TransportRequest::create(HttpMethod::Get, "https://[1:2:3:4:5:6:7:8]/")
                             .has_value())) return 1;
    if (!BSKY_TEST_CHECK(TransportRequest::create(HttpMethod::Get, "https://[::]/").has_value())) return 1;
    if (!BSKY_TEST_CHECK(TransportRequest::create(HttpMethod::Get, "https://[1::]/").has_value())) return 1;
    if (!BSKY_TEST_CHECK(TransportRequest::create(HttpMethod::Get, "https://[::ffff:192.0.2.128]/")
                             .has_value())) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get,
                                                    "https://[::ffff:192.0.2.128.]/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get,
                                                    "https://[::192.0.2.128.]/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get,
                                                    "https://[::ffff:256.0.0.1]/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get,
                                                    "https://[1:2:3:4:5:6:7:8:]/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://[1::2:]/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://[gggg::1:2]/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://[1:::2]/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://[fe80::1%25]/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://[fe80::1%25bad:zone]/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://[::1]:/"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://example.com/path\\name"))) return 1;
    if (!BSKY_TEST_CHECK(!TransportRequest::create(HttpMethod::Get, "https://example.com/path|name"))) return 1;

    auto request = TransportRequest::create(HttpMethod::Post, "https://pds.example/xrpc/com.example.echo");
    if (!BSKY_TEST_CHECK(request.has_value())) return 1;
    if (!BSKY_TEST_CHECK(request.value().set_header("Content-Type", "application/json").has_value())) return 1;
    if (!BSKY_TEST_CHECK(request.value().set_header("content-type", "application/cbor").has_value())) return 1;
    if (!BSKY_TEST_CHECK(request.value().header("CONTENT-TYPE") ==
                         std::optional<std::string>("application/cbor"))) return 1;
    if (!BSKY_TEST_CHECK(!request.value().set_header("Bad Header", "value"))) return 1;
    if (!BSKY_TEST_CHECK(!request.value().set_header("X-Test", "line\nvalue"))) return 1;
    if (!BSKY_TEST_CHECK(request.value().set_header("X-Test", "one").has_value())) return 1;
    if (!BSKY_TEST_CHECK(request.value().set_header("x-test", "two").has_value())) return 1;
    const std::vector<std::string> request_values{"two"};
    if (!BSKY_TEST_CHECK(request.value().headers("X-TEST") == request_values)) return 1;
    request.value().set_body(std::vector<std::uint8_t>{1U, 2U, 3U});
    if (!BSKY_TEST_CHECK(request.value().body().size() == 3U)) return 1;
    return 0;
}

int response_headers_and_limits() {
    auto response = TransportResponse::create(200U);
    if (!BSKY_TEST_CHECK(response.has_value())) return 1;
    if (!BSKY_TEST_CHECK(response.value().append_header("Set-Cookie", "a=1").has_value())) return 1;
    if (!BSKY_TEST_CHECK(response.value().append_header("set-cookie", "b=2").has_value())) return 1;
    const std::vector<std::string> cookie_values{"a=1", "b=2"};
    if (!BSKY_TEST_CHECK(response.value().headers("SET-COOKIE") == cookie_values)) return 1;
    if (!BSKY_TEST_CHECK(response.value().header("set-cookie") ==
                         std::optional<std::string>("a=1"))) return 1;
    if (!BSKY_TEST_CHECK(response.value().set_header("Set-Cookie", "c=3").has_value())) return 1;
    const std::vector<std::string> replaced_cookie{"c=3"};
    if (!BSKY_TEST_CHECK(response.value().headers("set-cookie") == replaced_cookie)) return 1;

    const auto before_invalid = response.value().headers();
    if (!BSKY_TEST_CHECK(!response.value().set_header("Bad Header", "value"))) return 1;
    if (!BSKY_TEST_CHECK(same_headers(response.value().headers(), before_invalid))) return 1;

    if (!BSKY_TEST_CHECK(!TransportResponse::create(99U))) return 1;
    if (!BSKY_TEST_CHECK(!TransportResponse::create(600U))) return 1;

    std::string oversized(16U * 1024U + 1U, 'x');
    if (!BSKY_TEST_CHECK(!response.value().append_header("X-Large", oversized))) return 1;
    for (std::size_t index = 0U; index < 128U; ++index) {
        if (!BSKY_TEST_CHECK(response.value().append_header("X-" + std::to_string(index), "v") ||
                             index >= 127U)) return 1;
    }
    if (!BSKY_TEST_CHECK(!response.value().append_header("X-Overflow", "v"))) return 1;

    const auto before_count_overflow = response.value().headers();
    if (!BSKY_TEST_CHECK(!response.value().set_header("X-Another", "v"))) return 1;
    if (!BSKY_TEST_CHECK(same_headers(response.value().headers(), before_count_overflow))) return 1;

    auto byte_limited = TransportResponse::create(200U);
    if (!BSKY_TEST_CHECK(byte_limited.has_value())) return 1;
    const std::string large_value(16U * 1024U, 'y');
    if (!BSKY_TEST_CHECK(byte_limited.value().set_header("A", large_value).has_value())) return 1;
    if (!BSKY_TEST_CHECK(byte_limited.value().set_header("B", large_value).has_value())) return 1;
    if (!BSKY_TEST_CHECK(byte_limited.value().set_header("C", large_value).has_value())) return 1;
    const auto before_bytes_overflow = byte_limited.value().headers();
    if (!BSKY_TEST_CHECK(!byte_limited.value().set_header("D", large_value))) return 1;
    if (!BSKY_TEST_CHECK(same_headers(byte_limited.value().headers(), before_bytes_overflow))) {
        return 1;
    }
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
    if (!BSKY_TEST_CHECK(response.has_value() && response.value().status() == 200U)) return 1;
    const std::vector<std::uint8_t> response_body{2U};
    if (!BSKY_TEST_CHECK(response.value().body() == response_body)) return 1;
    if (!BSKY_TEST_CHECK(transport.requests().size() == 1U)) return 1;
    request.value().set_body(std::vector<std::uint8_t>{1U});
    const std::vector<std::uint8_t> original_body{9U, 8U};
    if (!BSKY_TEST_CHECK(transport.requests().front().body() == original_body)) return 1;

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

int exception_and_concurrency() {
    FakeTransport empty;
    auto request = TransportRequest::create(HttpMethod::Get, "https://example.com/");
    if (!BSKY_TEST_CHECK(request.has_value())) return 1;
    auto missing = empty.send(request.value(), CancellationSource().token());
    if (!BSKY_TEST_CHECK(!missing.has_value() &&
                         missing.error().category() == ErrorCategory::Transport)) return 1;

    FakeTransport throwing([](const TransportRequest&, bsky_cpp::CancellationToken) {
        throw std::runtime_error("secret adapter detail");
        return bsky_cpp::Result<TransportResponse>::failure(
            bsky_cpp::Error(ErrorCategory::Internal, "unreachable"));
    });
    auto thrown = throwing.send(request.value(), CancellationSource().token());
    if (!BSKY_TEST_CHECK(!thrown.has_value() &&
                         thrown.error().category() == ErrorCategory::Transport &&
                         thrown.error().message().find("secret") == std::string::npos)) return 1;

    FakeTransport concurrent([](const TransportRequest&, bsky_cpp::CancellationToken) {
        return TransportResponse::create(204U);
    });
    std::vector<std::thread> workers;
    for (int index = 0; index < 4; ++index) {
        workers.emplace_back([&concurrent, &request]() {
            for (int iteration = 0; iteration < 25; ++iteration) {
                static_cast<void>(concurrent.send(request.value(), CancellationSource().token()));
            }
        });
    }
    for (std::thread& worker : workers) worker.join();
    if (!BSKY_TEST_CHECK(concurrent.requests().size() == 100U)) return 1;

    CancellationSource cooperative_source;
    std::atomic<bool> entered{false};
    std::atomic<bool> observed{false};
    FakeTransport cooperative([&](const TransportRequest&, bsky_cpp::CancellationToken token) {
        entered.store(true, std::memory_order_release);
        while (!token.is_cancellation_requested()) {
            std::this_thread::yield();
        }
        observed.store(true, std::memory_order_release);
        return TransportResponse::create(499U);
    });
    std::thread worker([&]() {
        static_cast<void>(cooperative.send(request.value(), cooperative_source.token()));
    });
    while (!entered.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    cooperative_source.request_cancellation();
    worker.join();
    if (!BSKY_TEST_CHECK(observed.load(std::memory_order_acquire))) return 1;
    return 0;
}

}  // namespace

int main() {
    if (request_validation() != 0) return 1;
    if (response_headers_and_limits() != 0) return 1;
    if (fake_transport_behavior() != 0) return 1;
    return exception_and_concurrency();
}
