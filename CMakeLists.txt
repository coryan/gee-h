cmake_minimum_required(VERSION 3.7)
project(gee-h)

set(CMAKE_CXX_STANDARD 14)
find_package(Protobuf REQUIRED)
enable_testing()

# add_subdirectory(ext/googletest/googletest)
add_subdirectory(ext/googletest/googlemock)

set(SOURCE_FILES gh/session_state.cpp gh/session_state.hpp)
include_directories("${PROJECT_SOURCE_DIR}"
        "${PROJECT_BINARY_DIR}"
        ${Protobuf_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS})

add_library(gee_h SHARED ${SOURCE_FILES})
add_executable(gh_session_state_ut gh/session_state_ut.cpp)

get_directory_property(all_targets DIRECTORY . BUILDSYSTEM_TARGETS )
foreach(target ${all_targets})
    get_target_property(tname ${target} "NAME")
    string(REGEX MATCH ".*_ut$" is_test ${tname})
    if(is_test)
        get_target_property(tname ${target} "NAME")
        target_link_libraries(${target} gee_h gtest gtest_main)
        add_test(${target} COMMAND ${tname})
        message(status " " ${tname} " -> " $<TARGET_FILE:${target}>)
    endif(is_test)
endforeach()
