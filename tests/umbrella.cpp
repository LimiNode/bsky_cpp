#include <bsky_cpp.hpp>

int main() {
    const bsky_cpp::Version value = bsky_cpp::version();
    return value.major == 0U && value.minor == 1U ? 0 : 1;
}
