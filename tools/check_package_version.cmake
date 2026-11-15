if(NOT DEFINED PREFIX OR NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "PREFIX, SOURCE_DIR, and OUTPUT are required")
endif()

if(NOT DEFINED BUILD_TYPE)
    set(BUILD_TYPE Debug)
endif()

function(check_package_version mode runtime)
    set(runtime_option)
    if(runtime)
        set(runtime_option -DBSKY_CONSUMER_RUNTIME=ON)
    endif()

    set(accepted_dir "${OUTPUT}/${mode}-accepted")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                -S "${SOURCE_DIR}/tests/consumer"
                -B "${accepted_dir}"
                -DCMAKE_PREFIX_PATH=${PREFIX}
                -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
                -DBSKY_CONSUMER_MIN_VERSION=0.1
                ${runtime_option}
        RESULT_VARIABLE accepted_result
        OUTPUT_VARIABLE accepted_output
        ERROR_VARIABLE accepted_error
    )
    if(NOT accepted_result EQUAL 0)
        message(FATAL_ERROR
            "${mode} same-minor package request was rejected\n"
            "${accepted_output}\n${accepted_error}")
    endif()

    set(rejected_dir "${OUTPUT}/${mode}-rejected")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                -S "${SOURCE_DIR}/tests/consumer"
                -B "${rejected_dir}"
                -DCMAKE_PREFIX_PATH=${PREFIX}
                -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
                -DBSKY_CONSUMER_MIN_VERSION=0.2
                ${runtime_option}
        RESULT_VARIABLE rejected_result
        OUTPUT_VARIABLE rejected_output
        ERROR_VARIABLE rejected_error
    )
    set(rejected_diagnostics "${rejected_output}\n${rejected_error}")
    if(rejected_result EQUAL 0)
        message(FATAL_ERROR
            "${mode} cross-minor package request was unexpectedly accepted")
    endif()
    if(NOT rejected_diagnostics MATCHES "compatible with requested version")
        message(FATAL_ERROR
            "${mode} package request did not fail for version incompatibility\n"
            "${rejected_diagnostics}")
    endif()
    if(NOT rejected_diagnostics MATCHES "0\\.2")
        message(FATAL_ERROR
            "${mode} package rejection did not report requested version 0.2\n"
            "${rejected_diagnostics}")
    endif()
endfunction()

check_package_version(foundation FALSE)
if(RUNTIME_CHECK)
    check_package_version(runtime TRUE)
endif()

message(STATUS "bsky_cpp package version compatibility checks passed")
