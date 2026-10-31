#include "bsky_cpp/core/version.hpp"

#include "test_support.hpp"

int main() {
    const bsky_cpp::Version header_version = bsky_cpp::version();
    const bsky_cpp::Version linked_version = bsky_cpp::linked_version();

    if (!BSKY_TEST_CHECK(header_version.major == 0)) {
        return 1;
    }
    if (!BSKY_TEST_CHECK(header_version.minor == 1)) {
        return 1;
    }
    if (!BSKY_TEST_CHECK(header_version.patch == 0)) {
        return 1;
    }
    if (!BSKY_TEST_CHECK(header_version.major == linked_version.major)) {
        return 1;
    }
    if (!BSKY_TEST_CHECK(header_version.minor == linked_version.minor)) {
        return 1;
    }
    if (!BSKY_TEST_CHECK(header_version.patch == linked_version.patch)) {
        return 1;
    }
    return 0;
}
