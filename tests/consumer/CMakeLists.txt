cmake_minimum_required(VERSION 3.20)
project(bsky_cpp_consumer LANGUAGES CXX)

include(CTest)

find_package(bsky_cpp CONFIG REQUIRED)

add_executable(bsky_cpp_consumer main.cpp)
target_compile_features(bsky_cpp_consumer PRIVATE cxx_std_17)
target_link_libraries(bsky_cpp_consumer PRIVATE bsky_cpp::bsky_cpp)

add_test(NAME bsky_cpp.consumer COMMAND bsky_cpp_consumer)
