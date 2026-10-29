cmake_minimum_required(VERSION 3.20)

project(bsky_cpp
    VERSION 0.1.0
    DESCRIPTION "Modern C++17 client SDK foundation for Bluesky and AT Protocol"
    LANGUAGES CXX
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
include(CTest)

option(BSKY_BUILD_TESTS "Build bsky_cpp tests" ${BUILD_TESTING})
option(BSKY_BUILD_EXAMPLES "Build bsky_cpp examples" OFF)
option(BSKY_ENABLE_SANITIZERS "Enable address and undefined-behaviour sanitizers" OFF)

set(BSKY_PUBLIC_HEADERS
    src/bsky_cpp/cancellation.hpp
    src/bsky_cpp/error.hpp
    src/bsky_cpp/identifiers.hpp
    src/bsky_cpp/result.hpp
    src/bsky_cpp/version.hpp
)

add_library(bsky_cpp STATIC
    src/bsky_cpp/cancellation.cpp
    src/bsky_cpp/error.cpp
    src/bsky_cpp/identifiers.cpp
    src/bsky_cpp/version.cpp
)
add_library(bsky_cpp::bsky_cpp ALIAS bsky_cpp)

target_compile_features(bsky_cpp PUBLIC cxx_std_17)
target_include_directories(bsky_cpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(bsky_cpp PROPERTIES
    EXPORT_NAME bsky_cpp
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

if(MSVC)
    target_compile_options(bsky_cpp PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(bsky_cpp PRIVATE -Wall -Wextra -Wpedantic)
endif()

function(bsky_cpp_enable_sanitizers target)
    if(BSKY_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

if(BSKY_ENABLE_SANITIZERS)
    bsky_cpp_enable_sanitizers(bsky_cpp)
endif()

if(BSKY_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(BSKY_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

install(TARGETS bsky_cpp
    EXPORT bsky_cppTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ${BSKY_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bsky_cpp
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/bsky_cppConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bsky_cppConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/bsky_cppConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/bsky_cpp"
)

install(EXPORT bsky_cppTargets
    FILE bsky_cppTargets.cmake
    NAMESPACE bsky_cpp::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/bsky_cpp"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/bsky_cppConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/bsky_cppConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/bsky_cpp"
)
