#include "bsky_cpp/version.hpp"

#include <iostream>

int main() {
    const bsky_cpp::Version current = bsky_cpp::linked_version();
    std::cout << current.major << '.' << current.minor << '.' << current.patch << '\n';
    return 0;
}
