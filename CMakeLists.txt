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

cmake_minimum_required(VERSION 3.5)

set(PACKAGE_NAME "Gee-H")
set(PACKAGE_VERSION "0.2")
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME "${PACKAGE_NAME}-${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "https://github.com/coryan/gee-h/issues")

project(Gee-H CXX C)

# Require C++14 for this project ...
set(CMAKE_CXX_STANDARD 14)
set(CXX_STANDARD_REQUIRED ON)

option(GEE_H_DISABLE_TESTS, "Disable unit and integration tests. Can be useful to avoid installing gmock+gtest." OFF)

# ... the author is paranoid.  Turn on all available warnings
# and turn warnings into errors to stop the build if any warning is
# emitted ...
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG(-Werror COMPILER_SUPPORTS_WERROR)
if (COMPILER_SUPPORTS_WERROR)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
endif ()
CHECK_CXX_COMPILER_FLAG(-Wall COMPILER_SUPPORTS_WALL)
if (COMPILER_SUPPORTS_WALL)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif ()
CHECK_CXX_COMPILER_FLAG(/WX COMPILER_SUPPORTS_WX)
if (COMPILER_SUPPORTS_WX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /WX")
endif ()
CHECK_CXX_COMPILER_FLAG(/W4 COMPILER_SUPPORTS_SWALL)
if (COMPILER_SUPPORTS_SWALL)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Wall")
endif ()

# ... include the functions to compile proto files ...
include(FindProtobuf)

# ... find the protobuf, grpc, and grpc++ libraries using pkg-config ...
include(FindPkgConfig)
pkg_check_modules(PROTOBUF REQUIRED protobuf>=3.0)
pkg_check_modules(GRPCPP REQUIRED grpc++>=1.0)
pkg_check_modules(GRPC REQUIRED grpc>=4.0)
link_directories(${GRPCPP_LIBRARY_DIRS} ${GRPC_LIBRARY_DIRS} ${PROTOBUF_LIBRARY_DIRS})
include_directories(${GRPCPP_INCLUDE_DIRS} ${GRPC_INCLUDE_DIRS} ${PROTOBUF_INCLUDE_DIRS})


# ... if set, enable code coverage builds for g++, and create a "test_coverage" target to collect the coverage using
# lcov.  My attempts at using the gcovr cobertura output failed miserably, so I am sticking to what I know ...
option(COVERAGE "Enable code coverage builds in Gee-H." OFF)
if (${COVERAGE})
    include(cmake/CodeCoverage.cmake)
    append_coverage_compiler_flags()
endif (${COVERAGE})

#
# Generate C++ sources for the etcd proto files.  It is not a huge collection, but the directory hierarchy is very
# finicky, see https://github.com/coryan/jaybeams/issues/158 for my rant on the subject.
#
include(cmake/CompileProtos.cmake)
set(PROTOBUF_IMPORT_DIRS "${PROJECT_SOURCE_DIR}/ext/googleapis"
        "${PROJECT_SOURCE_DIR}/ext/gogoproto"
        "${PROJECT_SOURCE_DIR}/ext"
        )
PROTOBUF_GENERATE_CPP(PROTO_SOURCES PROTO_HDRS
        ${PROJECT_SOURCE_DIR}/ext/googleapis/google/api/http.proto
        ${PROJECT_SOURCE_DIR}/ext/googleapis/google/api/annotations.proto
        ${PROJECT_SOURCE_DIR}/ext/gogoproto/gogoproto/gogo.proto
        ${PROJECT_SOURCE_DIR}/ext/etcd/mvcc/mvccpb/kv.proto
        ${PROJECT_SOURCE_DIR}/ext/etcd/auth/authpb/auth.proto
        ${PROJECT_SOURCE_DIR}/ext/etcd/etcdserver/etcdserverpb/rpc.proto)
GRPC_GENERATE_CPP(GRPCPP_SOURCES GRPCPP_HDRS GRPCPP_MOCKS
        ${PROJECT_SOURCE_DIR}/ext/etcd/etcdserver/etcdserverpb/rpc.proto)

include_directories("${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}")

# Compile things into a shared library, apparently it is easy to instruct cmake(1) to generate a static library even
# if we only describe how to create a shared lib.
add_library(gee_h SHARED
        gh/active_completion_queue.hpp
        gh/active_completion_queue.cpp
        gh/assert_throw.hpp
        gh/assert_throw.cpp
        gh/completion_queue.hpp
        gh/detail/async_op_counter.hpp
        gh/detail/async_op_counter.cpp
        gh/detail/async_rpc_op.hpp
        gh/detail/base_async_op.hpp
        gh/detail/base_completion_queue.hpp
        gh/detail/base_completion_queue.cpp
        gh/detail/cluster_membership_impl.hpp
        gh/detail/deadline_timer.hpp
        gh/detail/default_grpc_interceptor.hpp
        gh/detail/election_candidate_impl.hpp
        gh/detail/election_observer_impl.hpp
        gh/detail/rpc_retry_policy.hpp
        gh/detail/rpc_backoff_policy.hpp
        gh/detail/rpc_policies.hpp
        gh/detail/rpc_policies.cpp
        gh/detail/grpc_errors.hpp
        gh/detail/grpc_errors.cpp
        gh/detail/grpc_errors.cpp
        gh/detail/append_annotations.hpp
        gh/detail/mocked_grpc_interceptor.hpp
        gh/detail/null_stream.hpp
        gh/detail/stream_async_ops.hpp
        gh/detail/session_impl.hpp
        gh/detail/stream_future_status.hpp
        gh/election_candidate.hpp
        gh/election_candidate.cpp
        gh/election_observer.hpp
        gh/election_observer.cpp
        gh/leader_election.hpp
        gh/leader_election.cpp
        gh/etcd_client.hpp
        gh/log.hpp
        gh/log.cpp
        gh/log_severity.hpp
        gh/log_severity.cpp
        gh/log_sink.hpp
        gh/prefix_end.hpp
        gh/prefix_end.cpp
        gh/session.hpp
        gh/session.cpp
        gh/watch_election.hpp
        gh/watch_election.cpp
        ${PROTO_SOURCES} ${PROTO_HDRS}
        ${GRPCPP_SOURCES} ${GRPCPP_HEADERS}
        gh/etcd_client.cpp)
target_link_libraries(gee_h ${GRPCPP_LIBRARIES} ${GRPC_LIBRARIES} ${PROTOBUF_LIBRARIES})

add_executable(observe_election examples/observe_election.cpp)
target_link_libraries(observe_election gee_h ${GRPCPP_LIBRARIES} ${GRPC_LIBRARIES} ${PROTOBUF_LIBRARIES})
add_executable(join_election examples/join_election.cpp)
target_link_libraries(join_election gee_h ${GRPCPP_LIBRARIES} ${GRPC_LIBRARIES} ${PROTOBUF_LIBRARIES})

# ... determine if libraries are installed in the lib64 directory ...
get_property(LIB64 GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)
if ("${LIB64}" STREQUAL "TRUE")
    set(LIBSUFFIX 64)
else ()
    set(LIBSUFFIX "")
endif ()
set(INSTALL_LIB_DIR lib${LIBSUFFIX} CACHE PATH "Installation directory for libraries")

# ... generate a pkg-config for Gee-H ...
SET(PKG_CONFIG_REQUIRES "grpc++ grpc protobuf")
SET(PKG_CONFIG_LIBDIR "\${prefix}/lib")
SET(PKG_CONFIG_INCLUDEDIR "\${prefix}/include")
SET(PKG_CONFIG_LIBS "-L\${libdir} -lgee_h")
SET(PKG_CONFIG_CFLAGS "-I\${includedir}")
CONFIGURE_FILE(
        "${CMAKE_CURRENT_SOURCE_DIR}/templates/pkg-config.pc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc")

# ... define the install rules ...
install(TARGETS observe_election join_election gee_h
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION ${INSTALL_LIB_DIR})
install(DIRECTORY gh/ DESTINATION include/gh FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY ${CMAKE_BINARY_DIR}/etcd/ DESTINATION include/etcd FILES_MATCHING PATTERN "*.h")
install(DIRECTORY ${CMAKE_BINARY_DIR}/gogoproto/ DESTINATION include/gogoproto FILES_MATCHING PATTERN "*.h")
install(DIRECTORY ${CMAKE_BINARY_DIR}/google/ DESTINATION include/google FILES_MATCHING PATTERN "*.h")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc" DESTINATION lib/pkgconfig)

if (NOT GEE_H_DISABLE_TESTS)
    # ... turn on testing support in CMake ...
    enable_testing()

    # ... external dependencies and packages ...
    add_subdirectory(ext/googletest/googlemock)

    # ... all the unit tests, we setup the targets and dependencies in a loop ...
    set(all_unit_tests
            gh/active_completion_queue_ut.cpp
            gh/assert_throw_ut.cpp
            gh/completion_queue_ut.cpp
            gh/detail/async_op_counter_ut.cpp
            gh/detail/base_completion_queue_ut.cpp
            gh/detail/cluster_membership_impl_ut.cpp
            gh/detail/election_candidate_impl_ut.cpp
            gh/detail/election_observer_impl_ut.cpp
            gh/detail/rpc_policies_ut.cpp
            gh/detail/grpc_errors_ut.cpp
            gh/detail/mocked_grpc_interceptor_ut.cpp
            gh/detail/null_stream_ut.cpp
            gh/detail/session_impl_ut.cpp
            gh/detail/stream_future_status_ut.cpp
            gh/log_ut.cpp
            gh/log_severity_ut.cpp
            gh/log_sink_ut.cpp
            gh/prefix_end_ut.cpp
            )

    # ... add the right libraries to all the unit tests, and also turn them into tests ...
    set(ut_sources)
    foreach (fname ${all_unit_tests})
        string(REGEX MATCH ".*_ut.cpp$" is_ut ${fname})
        if (is_ut)
            string(REPLACE "/" "_" target ${fname})
            string(REPLACE ".cpp" "" target ${target})
            add_executable(${target} ${fname})
            get_target_property(tname ${target} NAME)
            target_link_libraries(${target} gmock gtest gtest_main gee_h grpc++ grpc protobuf)
            add_test(${tname} ${target})
            get_target_property(sources ${target} SOURCES)
            set(ut_sources ${ut_sources} ${sources})
        endif (is_ut)
    endforeach ()

    # ... a giant executable with all the unit tests, this is convenient when using CLion ...
    add_executable(all_unit_tests ${ut_sources})
    target_link_libraries(all_unit_tests gee_h gmock gtest gtest_main)

    # ... integration tests, using the unit test framework to report results ...
    add_executable(gh_session_test gh/session_test.cpp)
    target_link_libraries(gh_session_test gee_h gmock gtest gtest_main)
    add_executable(gh_leader_election_test gh/leader_election_test.cpp)
    target_link_libraries(gh_leader_election_test gee_h gmock gtest gtest_main)
    add_executable(gh_watch_election_test gh/watch_election_test.cpp)
    target_link_libraries(gh_watch_election_test gee_h gmock gtest gtest_main)

    add_test(NAME gh_integration_tests
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/gh/integration_tests.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif (NOT GEE_H_DISABLE_TESTS)

add_executable(watcher_and_compaction experiments/watcher_and_compaction.cpp)
target_link_libraries(watcher_and_compaction gee_h ${GRPCPP_LIBRARIES} ${GRPC_LIBRARIES} ${PROTOBUF_LIBRARIES})

add_executable(cluster_failure_behavior experiments/cluster_failure_behavior.cpp)
target_link_libraries(cluster_failure_behavior gee_h ${GRPCPP_LIBRARIES} ${GRPC_LIBRARIES} ${PROTOBUF_LIBRARIES})

file(GLOB_RECURSE ALL_CXX_SOURCE_FILES gh/*.[hc]pp examples/*.[hc]pp experiments/*.[hc]pp)
find_program(CLANG_FORMAT clang-format)
if (CLANG_FORMAT)
    add_custom_target(clang-format
            COMMAND ${CLANG_FORMAT} -i ${ALL_CXX_SOURCE_FILES})
    add_custom_target(check-style
            COMMAND git diff --exit-code
            DEPENDS clang-format)
else ()
    add_custom_target(check-style)
endif (CLANG_FORMAT)
