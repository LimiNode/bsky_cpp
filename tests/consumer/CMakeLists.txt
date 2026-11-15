cmake_minimum_required(VERSION 3.20)
project(bsky_cpp_consumer LANGUAGES CXX)

include(CTest)

option(BSKY_CONSUMER_RUNTIME "Exercise the installed runtime target" OFF)
set(BSKY_CONSUMER_MIN_VERSION "" CACHE STRING
    "Minimum bsky_cpp package version required by this consumer")
if(BSKY_CONSUMER_MIN_VERSION)
    set(_bsky_package_version_args ${BSKY_CONSUMER_MIN_VERSION})
else()
    set(_bsky_package_version_args)
endif()
if(BSKY_CONSUMER_RUNTIME)
    find_package(bsky_cpp ${_bsky_package_version_args} CONFIG REQUIRED COMPONENTS runtime)
else()
    find_package(bsky_cpp ${_bsky_package_version_args} CONFIG REQUIRED)
endif()

add_executable(bsky_cpp_consumer main.cpp)
target_compile_features(bsky_cpp_consumer PRIVATE cxx_std_17)
target_link_libraries(bsky_cpp_consumer PRIVATE bsky_cpp::bsky_cpp)
if(BSKY_CONSUMER_RUNTIME)
    target_compile_definitions(bsky_cpp_consumer PRIVATE BSKY_CONSUMER_RUNTIME=1)
    target_link_libraries(bsky_cpp_consumer PRIVATE bsky_cpp::runtime)
endif()

add_test(NAME bsky_cpp.consumer COMMAND bsky_cpp_consumer)
