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

set(PACKAGE_NAME "Gee-H")
set(PACKAGE_VERSION   "0.1")
set(PACKAGE_STRING    "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME   "${PACKAGE_NAME}-${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "https://github.com/coryan/gee-h/issues")

project(Gee-H CXX C)

# Require C++14 for this project ...
set(CXX_STANDARD_REQUIRED 14)
set(CMAKE_CXX_STANDARD ${CXX_STANDARD_REQUIRED}) # for older cmake

# ... the author is paranoid.  Turn on all available warnings
# and turn warnings into errors to stop the build if any warning is
# emitted ...
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG(-Werror COMPILER_SUPPORTS_WERROR)
if(COMPILER_SUPPORTS_WERROR)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
endif()
CHECK_CXX_COMPILER_FLAG(-Wall COMPILER_SUPPORTS_WALL)
if(COMPILER_SUPPORTS_WALL)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif()
CHECK_CXX_COMPILER_FLAG(/WX COMPILER_SUPPORTS_WX)
if(COMPILER_SUPPORTS_WX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /WX")
endif()
CHECK_CXX_COMPILER_FLAG(/W4 COMPILER_SUPPORTS_SWALL)
if(COMPILER_SUPPORTS_SWALL)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Wall")
endif()

# ... turn on testing support in CMake ...
enable_testing()

# ... external dependencies and packages ...
add_subdirectory(ext/googletest/googlemock)

# ... include the functions to compile proto files ...
include(FindProtobuf)

# ... find the protobuf, grpc, and grpc++ libraries using pkg-config ...
include(FindPkgConfig)
pkg_check_modules(PROTOBUF REQUIRED protobuf>=3.0)
pkg_check_modules(GRPCPP REQUIRED grpc++>=1.0)
pkg_check_modules(GRPC REQUIRED grpc>=4.0)

link_directories(${GRPCPP_LIBRARY_DIRS} ${GRPC_LIBRARY_DIRS} ${PROTOBUF_LIBRARY_DIRS})
include_directories(${GRPCPP_INCLUDE_DIRS} ${GRPC_INCLUDE_DIRS} ${PROTOBUF_INCLUDE_DIRS})

set(COVERAGE false CACHE BOOL "Enable code coverage builds.")
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
        gh/assert_throw.hpp
        gh/assert_throw.cpp
        gh/detail/async_ops.hpp
        gh/detail/base_completion_queue.hpp
        gh/detail/base_completion_queue.cpp
        gh/detail/deadline_timer.hpp
        gh/detail/default_grpc_interceptor.hpp
        gh/detail/null_stream.hpp
        gh/completion_queue.hpp
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
target_link_libraries(gee_h ${GRPCPP_LIBRARIES} ${GRPC_LIBRARIES} ${PROTOBUF_LIBRARIES})

add_executable(gh_assert_throw_ut gh/assert_throw_ut.cpp)
add_executable(gh_completion_queue_ut gh/completion_queue_ut.cpp)
add_executable(gh_detail_null_stream_ut gh/detail/null_stream_ut.cpp)
add_executable(gh_detail_base_completion_queue_ut gh/detail/base_completion_queue_ut.cpp)
add_executable(gh_log_ut gh/log_ut.cpp)
add_executable(gh_log_severity_ut gh/log_severity_ut.cpp)
add_executable(gh_log_sink_ut  gh/log_sink_ut.cpp)
add_executable(gh_prefix_end_ut gh/prefix_end_ut.cpp)
add_executable(gh_session_state_ut gh/session_state_ut.cpp)

# In cmake-3.7 we could use BUILDSYSTEM_TARGETS to get the list of all targets instead of manually maintaining the
# list here:
set(all_unit_tests
        gh_assert_throw_ut
        gh_completion_queue_ut
        gh_detail_null_stream_ut
        gh_detail_base_completion_queue_ut
        gh_log_ut
        gh_log_severity_ut
        gh_log_sink_ut
        gh_prefix_end_ut
        gh_session_state_ut
    )

# Add the right libraries to all the unit tests, and also turn them into tests.
set(ut_sources)
foreach(target ${all_unit_tests})
    get_target_property(tname ${target} NAME)
    string(REGEX MATCH ".*_ut$" is_ut ${tname})
    if(is_ut)
        get_target_property(tname ${target} NAME)
        target_link_libraries(${target} gmock gtest gtest_main gee_h grpc++ grpc protobuf)
        add_test(${tname} ${target})
        get_target_property(sources ${target} SOURCES)
        set(ut_sources ${ut_sources} ${sources})
    endif()
endforeach()

add_executable(all_unit_tests ${ut_sources})
target_link_libraries(all_unit_tests gee_h gmock gtest gtest_main)
