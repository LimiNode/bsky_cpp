#include <bsky_cpp.hpp>

#ifdef BSKY_CONSUMER_RUNTIME
#include <bsky_cpp/runtime/runtime.hpp>
#endif

#include <string>
#include <chrono>
#include <atomic>

int main() {
    bsky_cpp::Result<int> result(42);
    if (!result.has_value() || result.value() != 42) {
        return 1;
    }

    bsky_cpp::Error error(bsky_cpp::ErrorCategory::Internal, "consumer error");
    error.add_metadata("api_key", "must be redacted");
    if (error.metadata().size() != 1 ||
        error.metadata().front().value != "<redacted>") {
        return 1;
    }
    if (std::string(bsky_cpp::error_category_name(error.category())) != "internal") {
        return 1;
    }

    bsky_cpp::CancellationSource source;
    const bsky_cpp::CancellationToken token = source.token();
    if (token.is_cancellation_requested() || !source.request_cancellation() ||
        !token.is_cancellation_requested()) {
        return 1;
    }

    auto did = bsky_cpp::Did::parse("did:plc:z72i7hdynmk6r22z27s6tvur");
    auto handle = bsky_cpp::Handle::parse("Alice.Example.COM");
    auto uri = bsky_cpp::AtUri::parse(
        "at://alice.example.com/com.atproto.repo.create/3jfc");
    auto cid = bsky_cpp::Cid::parse(
        "bafyreiaaaebagbafaydqqcikbmga2dqpcaireeyuculbogazdinryhi6d4");
    auto blob_cid = bsky_cpp::Cid::parse(
        "bafkreiaaaebagbafaydqqcikbmga2dqpcaireeyuculbogazdinryhi6d4");
    if (!did || !handle || !uri || !cid || !cid.value().is_atproto_record() ||
        !blob_cid || !blob_cid.value().is_atproto_blob()) {
        return 1;
    }
    auto strong_ref = bsky_cpp::StrongRef::create(uri.value(), cid.value());
    if (!strong_ref) {
        return 1;
    }
    auto request = bsky_cpp::TransportRequest::create(
        bsky_cpp::HttpMethod::Get, "https://example.com/health");
    auto response = bsky_cpp::TransportResponse::create(204U);
    if (!request || !response) {
        return 1;
    }
#ifdef BSKY_CONSUMER_RUNTIME
    bsky_cpp::runtime::Runtime runtime;
    if (!runtime.start()) return 1;
    std::atomic<bool> delivered{false};
    auto operation = runtime.submit([](bsky_cpp::CancellationToken) {
        return bsky_cpp::Result<void>::success();
    }, [&](bsky_cpp::Result<void> completion) { delivered = completion.has_value(); });
    if (!operation || !operation.value().wait_for_completion(std::chrono::seconds(1)) ||
        !operation.value().await_callback_quiescence(std::chrono::seconds(1)) ||
        !delivered || !runtime.shutdown(std::chrono::seconds(1))) {
        return 1;
    }
#endif
    return 0;
}
