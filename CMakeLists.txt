#   Copyright 2017 Carlos O'Ryan
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#

cmake_minimum_required(VERSION 3.5)
project(gee-h)

set(CMAKE_CXX_STANDARD 14)
enable_testing()

add_subdirectory(ext/googletest/googlemock)

set(COVERAGE  false CACHE BOOL "Enable code coverage builds.")
if(${COVERAGE})
    include(cmake/CodeCoverage.cmake)
    include(ProcessorCount)
    ProcessorCount(NCPUS)
    set(ctest_parallelism "")
    if(NOT N EQUAL 0)
        set(ctest_parallelism "-j ${NCPUS}")
    endif()
    APPEND_COVERAGE_COMPILER_FLAGS()
    SETUP_TARGET_FOR_COVERAGE(
            NAME test_coverage
            EXECUTABLE ctest ${ctest_parallelism}
    )
endif(${COVERAGE})

include_directories("${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}")

add_library(gee_h SHARED
        gh/detail/null_stream.hpp
        gh/log.hpp
        gh/log.cpp
        gh/log_severity.hpp
        gh/log_severity.cpp
        gh/log_sink.hpp
        gh/prefix_end.hpp
        gh/prefix_end.cpp
        gh/session_state.cpp
        gh/session_state.hpp
    )
add_executable(gh_detail_null_stream_ut gh/detail/null_stream_ut.cpp)
add_executable(gh_log_ut gh/log_ut.cpp)
add_executable(gh_log_severity_ut gh/log_severity_ut.cpp)
add_executable(gh_log_sink_ut  gh/log_sink_ut.cpp)
add_executable(gh_prefix_end_ut gh/prefix_end_ut.cpp)
add_executable(gh_session_state_ut gh/session_state_ut.cpp)

# In cmake-3.7 we could use BUILDSYSTEM_TARGETS to get the list of all targets instead of manually maintaining the
# list here:
set(all_unit_tests
        gh_detail_null_stream_ut
        gh_log_ut
        gh_log_severity_ut
        gh_log_sink_ut
        gh_prefix_end_ut
        gh_session_state_ut
    )

# Add the right libraries to all the unit tests, and also turn them into tests.
foreach(target ${all_unit_tests})
    get_target_property(tname ${target} "NAME")
    string(REGEX MATCH ".*_ut$" is_ut ${tname})
    if(is_ut)
        get_target_property(tname ${target} "NAME")
        target_link_libraries(${target} gee_h gmock gtest gtest_main)
        add_test(${tname} ${target})
    endif(is_ut)
endforeach()
