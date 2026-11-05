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
option(BSKY_BUILD_RUNTIME "Build the optional event-hub-backed operation runtime" OFF)

# Explicit install surface; add public .ipp/.tpp fragments here when needed.
set(BSKY_FOUNDATION_PUBLIC_HEADERS
    src/bsky_cpp.hpp
    src/bsky_cpp/core/bsky_error.hpp
    src/bsky_cpp/core/cancellation.hpp
    src/bsky_cpp/core/result.hpp
    src/bsky_cpp/core/version.hpp
    src/bsky_cpp/atproto/identifiers.hpp
    src/bsky_cpp/transport/transport.hpp
)

set(BSKY_RUNTIME_PUBLIC_HEADERS
    src/bsky_cpp/runtime/runtime.hpp
)
set(BSKY_PUBLIC_HEADERS ${BSKY_FOUNDATION_PUBLIC_HEADERS})
if(BSKY_BUILD_RUNTIME)
    list(APPEND BSKY_PUBLIC_HEADERS ${BSKY_RUNTIME_PUBLIC_HEADERS})
endif()

# Canonical public-header names, derived from the source allowlist. Every
# install, documentation, and compile-only API check is generated from this
# list; private detail headers are intentionally absent.
set(BSKY_PUBLIC_HEADER_NAMES)
foreach(header IN LISTS BSKY_PUBLIC_HEADERS)
    file(RELATIVE_PATH header_name "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/${header}")
    list(APPEND BSKY_PUBLIC_HEADER_NAMES "${header_name}")
endforeach()

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

if(BSKY_BUILD_RUNTIME)
    # Prefer an existing/installed target. Source fallback composes the one
    # pinned submodule with optional upstream integrations disabled locally.
    function(bsky_cpp_find_event_hub)
        if(TARGET event_hub::event_hub)
            return()
        endif()
        find_package(event-hub-cpp CONFIG QUIET)
        if(TARGET event_hub::event_hub)
            return()
        endif()
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/event-hub-cpp/CMakeLists.txt")
            message(FATAL_ERROR "Install event-hub-cpp or run: git submodule update --init external/event-hub-cpp")
        endif()
        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
        set(EVENT_HUB_CPP_BUILD_EXAMPLES OFF)
        set(EVENT_HUB_CPP_BUILD_TESTS OFF)
        set(EVENT_HUB_CPP_USE_TIME_SHIELD OFF)
        set(EVENT_HUB_CPP_USE_TIME_SHIELD_NTP OFF)
        add_subdirectory(external/event-hub-cpp ${CMAKE_CURRENT_BINARY_DIR}/external/event-hub-cpp)
    endfunction()
    bsky_cpp_find_event_hub()

    find_package(Threads REQUIRED)
    add_library(bsky_cpp_runtime STATIC
        src/bsky_cpp/runtime/runtime.cpp
    )
    add_library(bsky_cpp::runtime ALIAS bsky_cpp_runtime)
    target_compile_features(bsky_cpp_runtime PUBLIC cxx_std_17)
    target_include_directories(bsky_cpp_runtime
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(bsky_cpp_runtime
        PUBLIC bsky_cpp::bsky_cpp event_hub::event_hub Threads::Threads
    )
    set_target_properties(bsky_cpp_runtime PROPERTIES EXPORT_NAME runtime)
    if(MSVC)
        target_compile_options(bsky_cpp_runtime PRIVATE /W4)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(bsky_cpp_runtime PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(BSKY_ENABLE_SANITIZERS)
        bsky_cpp_enable_sanitizers(bsky_cpp_runtime)
    endif()
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
    set(BSKY_DOXYGEN_INPUT)
    foreach(header IN LISTS BSKY_PUBLIC_HEADERS)
        string(APPEND BSKY_DOXYGEN_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/${header} ")
    endforeach()
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
foreach(header IN LISTS BSKY_PUBLIC_HEADERS)
    file(RELATIVE_PATH header_name "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/${header}")
    get_filename_component(header_directory "${header_name}" DIRECTORY)
    if(header_directory STREQUAL ".")
        set(header_destination "${CMAKE_INSTALL_INCLUDEDIR}")
    else()
        set(header_destination "${CMAKE_INSTALL_INCLUDEDIR}/${header_directory}")
    endif()
    install(FILES "${header}" DESTINATION "${header_destination}")
endforeach()

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/bsky_cppConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bsky_cppConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/bsky_cppConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/bsky_cpp"
)

if(BSKY_BUILD_RUNTIME)
    install(TARGETS bsky_cpp_runtime
        EXPORT bsky_cppRuntimeTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(EXPORT bsky_cppRuntimeTargets
        FILE bsky_cppRuntimeTargets.cmake
        NAMESPACE bsky_cpp::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/bsky_cpp"
    )
endif()

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
