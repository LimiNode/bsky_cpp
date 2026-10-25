#include "bsky_cpp/version.hpp"

#include <cassert>

int main() {
    const bsky_cpp::Version header_version = bsky_cpp::version();
    const bsky_cpp::Version linked_version = bsky_cpp::linked_version();

    assert(header_version.major == 0);
    assert(header_version.minor == 1);
    assert(header_version.patch == 0);
    assert(header_version.major == linked_version.major);
    assert(header_version.minor == linked_version.minor);
    assert(header_version.patch == linked_version.patch);
    return 0;
}
