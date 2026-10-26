#pragma once

#include <iostream>

namespace bsky_cpp_test {

inline bool check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
    }
    return condition;
}

}  // namespace bsky_cpp_test

#define BSKY_TEST_CHECK(condition) \
    ::bsky_cpp_test::check((condition), #condition, __FILE__, __LINE__)
