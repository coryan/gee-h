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

# Verify that an installed version of Gee-H is usable.
cmake_minimum_required(VERSION 3.5)

project(Gee-H-Test-Install CXX C)

# Require C++14 for this project ...
set(CMAKE_CXX_STANDARD 14)
set(CXX_STANDARD_REQUIRED ON)

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

# ... find the protobuf, grpc, and grpc++ libraries using pkg-config ...
include(FindPkgConfig)
pkg_check_modules(GEE_H REQUIRED Gee-H)
pkg_check_modules(PROTOBUF REQUIRED protobuf>=3.0)
pkg_check_modules(GRPCPP REQUIRED grpc++>=1.0)
pkg_check_modules(GRPC REQUIRED grpc>=4.0)
link_directories(${GEE_H_LIBRARY_DIRS} ${GRPCPP_LIBRARY_DIRS} ${GRPC_LIBRARY_DIRS} ${PROTOBUF_LIBRARY_DIRS})
include_directories(${GEE_H_INCLUDE_DIRS} ${GRPCPP_INCLUDE_DIRS} ${GRPC_INCLUDE_DIRS} ${PROTOBUF_INCLUDE_DIRS})

add_executable(election_participant_test ../../examples/join_election.cpp)
target_link_libraries(election_participant_test
        ${GEE_H_LIBRARIES} ${GRPCPP_LIBRARIES} ${GRPC_LIBRARIES} ${PROTOBUF_LIBRARIES})
