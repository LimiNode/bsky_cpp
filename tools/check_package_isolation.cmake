# Run after a runtime-enabled install. Use a fresh OUTPUT below build/.
foreach(required IN ITEMS PREFIX OUTPUT SOURCE_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
file(COPY "${PREFIX}/" DESTINATION "${OUTPUT}/prefix"
    PATTERN "event_hub" EXCLUDE
    PATTERN "event_hub.hpp" EXCLUDE
    PATTERN "event-hub-cpp*" EXCLUDE)
if(EXISTS "${OUTPUT}/prefix/include/bsky_cpp/runtime/detail")
    message(FATAL_ERROR "Private runtime headers were installed")
endif()
set(common -DCMAKE_PREFIX_PATH=${OUTPUT}/prefix
    -DCMAKE_DISABLE_FIND_PACKAGE_event-hub-cpp=TRUE
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE})
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/tests/consumer"
    -B "${OUTPUT}/foundation" ${common} -DBSKY_CONSUMER_RUNTIME=OFF
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Foundation unexpectedly requires event-hub")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${OUTPUT}/foundation" --config "${BUILD_TYPE}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Isolated foundation consumer did not link")
endif()
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${OUTPUT}/foundation"
    --build-config "${BUILD_TYPE}" --output-on-failure RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Isolated foundation consumer failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/tests/consumer"
    -B "${OUTPUT}/runtime" ${common} -DBSKY_CONSUMER_RUNTIME=ON
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "event-hub-cpp")
    message(FATAL_ERROR "Missing runtime dependency was not diagnosed: ${output}${error}")
endif()
message(STATUS "Package isolation passed: foundation works, missing runtime dependency fails")
