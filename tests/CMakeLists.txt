add_executable(bsky_cpp_smoke
    smoke.cpp
)
target_link_libraries(bsky_cpp_smoke PRIVATE bsky_cpp::bsky_cpp)
bsky_cpp_enable_sanitizers(bsky_cpp_smoke)

add_test(NAME bsky_cpp.smoke COMMAND bsky_cpp_smoke)
set_tests_properties(bsky_cpp.smoke PROPERTIES TIMEOUT 10)

find_package(Threads REQUIRED)
add_executable(bsky_cpp_primitives
    primitives.cpp
)
target_link_libraries(bsky_cpp_primitives PRIVATE bsky_cpp::bsky_cpp Threads::Threads)
bsky_cpp_enable_sanitizers(bsky_cpp_primitives)

add_test(NAME bsky_cpp.primitives COMMAND bsky_cpp_primitives)
set_tests_properties(bsky_cpp.primitives PROPERTIES TIMEOUT 10)

add_executable(bsky_cpp_identifiers
    identifiers.cpp
)
target_link_libraries(bsky_cpp_identifiers PRIVATE bsky_cpp::bsky_cpp)
bsky_cpp_enable_sanitizers(bsky_cpp_identifiers)

add_test(NAME bsky_cpp.identifiers COMMAND bsky_cpp_identifiers)
set_tests_properties(bsky_cpp.identifiers PROPERTIES TIMEOUT 10)

add_executable(bsky_cpp_identifier_fixtures
    identifier_fixtures.cpp
)
target_compile_definitions(bsky_cpp_identifier_fixtures PRIVATE
    BSKY_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/upstream"
)
target_link_libraries(bsky_cpp_identifier_fixtures PRIVATE bsky_cpp::bsky_cpp)
bsky_cpp_enable_sanitizers(bsky_cpp_identifier_fixtures)

add_test(NAME bsky_cpp.identifier_fixtures COMMAND bsky_cpp_identifier_fixtures)
set_tests_properties(bsky_cpp.identifier_fixtures PROPERTIES TIMEOUT 10)

add_executable(bsky_cpp_transport
    transport.cpp
)
target_link_libraries(bsky_cpp_transport PRIVATE bsky_cpp::bsky_cpp)
bsky_cpp_enable_sanitizers(bsky_cpp_transport)

add_test(NAME bsky_cpp.transport COMMAND bsky_cpp_transport)
set_tests_properties(bsky_cpp.transport PROPERTIES TIMEOUT 10)
