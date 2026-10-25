#include <bsky_cpp/cancellation.hpp>
#include <bsky_cpp/error.hpp>
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
    return 0;
}
