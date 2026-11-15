if(NOT DEFINED PREFIX OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "PREFIX, SOURCE_DIR, and OUTPUT are required")
endif()

if(NOT DEFINED BUILD_TYPE)
    set(BUILD_TYPE Debug)
endif()

set(accepted_dir "${OUTPUT}/accepted")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
            -S "${SOURCE_DIR}/tests/consumer"
            -B "${accepted_dir}"
            -DCMAKE_PREFIX_PATH=${PREFIX}
            -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
            -DBSKY_CONSUMER_MIN_VERSION=0.1
    RESULT_VARIABLE accepted_result
    OUTPUT_VARIABLE accepted_output
    ERROR_VARIABLE accepted_error
)
if(NOT accepted_result EQUAL 0)
    message(FATAL_ERROR
        "same-minor package request was rejected\n${accepted_output}\n${accepted_error}")
endif()

set(rejected_dir "${OUTPUT}/rejected")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
            -S "${SOURCE_DIR}/tests/consumer"
            -B "${rejected_dir}"
            -DCMAKE_PREFIX_PATH=${PREFIX}
            -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
            -DBSKY_CONSUMER_MIN_VERSION=0.2
    RESULT_VARIABLE rejected_result
    OUTPUT_VARIABLE rejected_output
    ERROR_VARIABLE rejected_error
)
if(rejected_result EQUAL 0)
    message(FATAL_ERROR
        "cross-minor package request was unexpectedly accepted\n${rejected_output}")
endif()

message(STATUS "bsky_cpp package version compatibility checks passed")
