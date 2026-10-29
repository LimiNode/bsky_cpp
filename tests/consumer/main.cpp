#include <bsky_cpp/cancellation.hpp>
#include <bsky_cpp/error.hpp>
#include <bsky_cpp/identifiers.hpp>
#include <bsky_cpp/result.hpp>

#include <string>

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
    return 0;
}
