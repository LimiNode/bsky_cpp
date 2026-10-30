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
option(BSKY_BUILD_DOCUMENTATION "Build bsky_cpp Doxygen documentation" OFF)

# Explicit install surface; add public .ipp/.tpp fragments here when needed.
set(BSKY_PUBLIC_HEADERS
    src/bsky_cpp.hpp
    src/bsky_cpp/core/bsky_error.hpp
    src/bsky_cpp/core/cancellation.hpp
    src/bsky_cpp/core/result.hpp
    src/bsky_cpp/core/version.hpp
    src/bsky_cpp/atproto/identifiers.hpp
    src/bsky_cpp/transport/transport.hpp
)

set(BSKY_CORE_SOURCES
    src/bsky_cpp/core/cancellation.cpp
    src/bsky_cpp/core/bsky_error.cpp
    src/bsky_cpp/core/version.cpp
)

add_library(bsky_cpp STATIC
    ${BSKY_CORE_SOURCES}
    src/bsky_cpp/atproto/identifiers.cpp
    src/bsky_cpp/transport/transport.cpp
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

if(BSKY_BUILD_DOCUMENTATION)
    find_package(Doxygen REQUIRED)
    set(BSKY_DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/docs/doxygen")
    file(MAKE_DIRECTORY "${BSKY_DOXYGEN_OUTPUT_DIRECTORY}")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Doxyfile.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        @ONLY
    )
    add_custom_target(bsky_cpp_docs
        COMMAND Doxygen::doxygen "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building bsky_cpp Doxygen documentation"
        VERBATIM
    )
endif()

install(TARGETS bsky_cpp
    EXPORT bsky_cppTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES src/bsky_cpp.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES
    src/bsky_cpp/core/bsky_error.hpp
    src/bsky_cpp/core/cancellation.hpp
    src/bsky_cpp/core/result.hpp
    src/bsky_cpp/core/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bsky_cpp/core
)
install(FILES
    src/bsky_cpp/atproto/identifiers.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bsky_cpp/atproto
)
install(FILES
    src/bsky_cpp/transport/transport.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bsky_cpp/transport
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
