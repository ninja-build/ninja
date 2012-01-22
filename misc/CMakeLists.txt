# Copyright 2011 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#

cmake_minimum_required(VERSION 2.8.4)

get_filename_component(topdir ${CMAKE_SOURCE_DIR}/.. REALPATH)
set(srcdir ${topdir}/src)



#
# Options
#

macro(ninja_option opt_name opt_msg opt_default)
    option(${opt_name} ${opt_msg} ${opt_default})
    list(APPEND options ${opt_name})
    set(${opt_name}_msg ${opt_msg})
endmacro()

macro(ninja_option_list)
    foreach(opt ${options})
        message(STATUS "    -D${opt}=${${opt}}   (${${opt}_msg})")
    endforeach()
endmacro()

ninja_option(platform        "target platform (linux/windows)"       OFF)
ninja_option(compiler        "cross-compiler"                        OFF)
ninja_option(debug           "enable debugging flags"                OFF)
ninja_option(verbose         "verbose output while building"         OFF)
ninja_option(gtest           "use gtest unpacked in given directory" OFF)
ninja_option(monolithic-test "build tests into one binary"           ON)

if(platform)
    set(${platform} 1)
elseif(UNIX)
    set(linux 1)
elseif(WIN32)
    set(windows 1)
endif()

if(debug)
    set(CMAKE_BUILD_TYPE Debug)
else()
    set(CMAKE_BUILD_TYPE Release)
endif()

# cross compile
if(UNIX AND windows)
    set(CMAKE_SYSTEM_NAME Windows)
    if(NOT compiler)
        set(compiler i586-mingw32msvc)
    endif()
    set(CMAKE_C_COMPILER ${compiler}-cc)
    set(CMAKE_CXX_COMPILER ${compiler}-c++)
    set(CMAKE_RC_COMPILER ${compiler}-windres)
    set(CMAKE_FIND_ROOT_PATH /usr/${compiler})
endif()

project(ninja)

if(verbose)
    set(CMAKE_VERBOSE_MAKEFILE ON)
endif()

message(STATUS)
message(STATUS "Options:")
ninja_option_list()
message(STATUS)



#
# Compiler/Linker settings
#

if(CMAKE_COMPILER_IS_GNUCXX)
    set(cflags "-Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-exceptions")
    if(debug)
        set(cflags "${cflags} -D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC")
    endif()
    if(linux)
        set(platformlibs pthread)
        set(cflags "${cflags} -fvisibility=hidden")
    elseif(windows)
        set(platformlibs shlwapi.lib iberty)
    endif()
elseif(MSVC)
    set(cflags "/WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(platformlibs shlwapi.lib)
endif()

set(CMAKE_C_FLAGS    "${CMAKE_C_FLAGS_INIT}   ${cflags}")
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS_INIT} ${cflags}")



#
# Source files
#

set(ninja_lib
        ${srcdir}/build.cc
        ${srcdir}/build_log.cc
        ${srcdir}/clean.cc
        ${srcdir}/depfile_parser.cc
        ${srcdir}/disk_interface.cc
        ${srcdir}/edit_distance.cc
        ${srcdir}/eval_env.cc
        ${srcdir}/graph.cc
        ${srcdir}/graphviz.cc
        ${srcdir}/lexer.cc
        ${srcdir}/metrics.cc
        ${srcdir}/parsers.cc
        ${srcdir}/state.cc
        ${srcdir}/util.cc)

if(linux)
    list(APPEND  ninja_lib ${srcdir}/subprocess.cc)
elseif(windows)
    list(APPEND ninja_lib ${srcdir}/getopt.c ${srcdir}/subprocess-win32.cc)
endif()

if(linux)
    set(inline ${CMAKE_BINARY_DIR}/concrete_inline.sh)
    file(WRITE ${inline}
    "mkdir -p ${CMAKE_BINARY_DIR}/build
     ${srcdir}/inline.sh kBrowsePy < ${srcdir}/browse.py > ${CMAKE_BINARY_DIR}/build/browse_py.h\n")
    list(APPEND  ninja_lib ${srcdir}/browse.cc)
    execute_process(COMMAND sh ${inline})
    include_directories(${CMAKE_BINARY_DIR}/build)
    #TODO FindPtyhon
    add_definitions(-DNINJA_PYTHON="python")
endif()

# find all headers
file(GLOB ninja_h ${srcdir}/*.h)



#
# ninja library, needed by 'ninja' and tests
#
add_library(ninja_lib STATIC ${ninja_lib} ${ninja_h})



#
# ninja executable
#
add_executable(ninja ${srcdir}/ninja.cc)
target_link_libraries(ninja ninja_lib ${platformlibs})



#
# Tests, gtest should be the path to gtest sources
#
if(gtest)
    enable_testing()

    # build gtest, test.cc only once
    add_library(testlib ${srcdir}/test.cc ${gtest}/src/gtest-all.cc ${gtest}/src/gtest_main.cc)
    set_target_properties(testlib PROPERTIES COMPILE_FLAGS "-I${gtest} -I${gtest}/include")

    # building a test is always the same
    macro(add_ninja_test test_name)
        set(test_sources ${ARGN})
        add_executable(${test_name} ${test_sources} ${ninja_h})
        target_link_libraries(${test_name} testlib ninja_lib ${platformlibs})
        set_target_properties(${test_name} PROPERTIES COMPILE_FLAGS "-I${gtest}/include")
        message(STATUS "Adding test '${test_name}'.")
    endmacro()

    # find all tests in src/
    file(GLOB ninja_test_sources ${srcdir}/*_test.cc)

    if(monolithic-test)
        add_ninja_test(ninja_test ${ninja_test_sources})
        add_test(NAME ninja_test COMMAND ninja_test)
    else()
        foreach(it ${ninja_test_sources})
            get_filename_component(file_name ${it} NAME_WE)
            add_ninja_test(${file_name} ${it})
            add_test(NAME ${file_name} COMMAND ${file_name})
        endforeach()
    endif()

    # not added as test because it breaks 'make test'
    add_ninja_test(parser_perftest ${srcdir}/parser_perftest.cc)
endif()

