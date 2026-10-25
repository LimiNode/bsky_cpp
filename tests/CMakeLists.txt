add_executable(bsky_cpp_smoke
    smoke.cpp
)
target_link_libraries(bsky_cpp_smoke PRIVATE bsky_cpp::bsky_cpp)

add_test(NAME bsky_cpp.smoke COMMAND bsky_cpp_smoke)
