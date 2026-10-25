#include <bsky_cpp/version.hpp>

int main() {
    const bsky_cpp::Version current = bsky_cpp::linked_version();
    return current.major == 0 && current.minor == 1 && current.patch == 0 ? 0 : 1;
}
