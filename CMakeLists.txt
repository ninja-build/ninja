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

if(ninja_srcdir)
    set(srcdir ${ninja_srcdir}/src)
else()
    get_filename_component(topdir ${CMAKE_SOURCE_DIR} REALPATH)
    set(srcdir ${topdir}/src)
endif()



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
ninja_option(compiler        "compiler (default/clang)"              OFF)
ninja_option(debug           "enable debugging flags"                OFF)
ninja_option(verbose         "verbose output while building"         OFF)
ninja_option(gtest           "use gtest unpacked in given directory" OFF)
ninja_option(monolithic-test "build tests into one binary"           ON)

if(platform)
    set(${platform} 1)
elseif(UNIX)
    message(STATUS "platform=linux")
    set(linux 1)
elseif(WIN32)
    message(STATUS "platform=windows")
    set(windows 1)
    add_definitions(-D_WIN32_WINNT=0x0501)
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
        set(cross i586-mingw32msvc)
    endif()
    set(CMAKE_C_COMPILER ${cross}-cc)
    set(CMAKE_CXX_COMPILER ${cross}-c++)
    set(CMAKE_RC_COMPILER ${cross}-windres)
    set(CMAKE_FIND_ROOT_PATH /usr/${cross})
endif()

if(compiler STREQUAL clang)
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
endif()

# this triggers  the compiler detection
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
    set(cflags "${cflags} -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fno-exceptions -pipe")
    set(nortti -fno-rtti)
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
    set(nortti /GR-)
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/EHsc" "/EHsc-" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX)
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
        ${srcdir}/explain.cc
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


set (RE2C_FOUND FALSE)
find_program (RE2C_EXECUTABLE "re2c")
if (RE2C_EXECUTABLE)
    set (RE2C_FOUND TRUE)
    set (RE2C_FLAGS -b -i --no-generation-date)

    # build src/depfile_parser.cc: re2c src/depfile_parser.in.cc
    # build src/lexer.cc: re2c src/lexer.in.cc
    foreach(_in depfile_parser.in.cc lexer.in.cc)
        string(REGEX REPLACE ".in.cc" ".cc" _out "${_in}")
        message(STATUS "Generate re2c src/${_in} -o src/${_out}")

         # CMake atrocity: if none of these OUTPUT files is used in a target in
         # the current CMakeLists.txt file, the ADD_CUSTOM_COMMAND is plainly
         # ignored and left out of the make files.
        add_custom_command(OUTPUT ${srcdir}/${_out}
            COMMAND ${RE2C_EXECUTABLE} ${${RE2C_FLAGS}} -o ${srcdir}/${_out} ${srcdir}/${_in}
            DEPENDS ${srcdir}/${_in}
        )
    endforeach()
endif (RE2C_EXECUTABLE)


find_package(PythonInterp)
if(PYTHONINTERP_FOUND)
    if(linux)
        set(inline ${CMAKE_BINARY_DIR}/concrete_inline.sh)
        file(WRITE ${inline}
        "mkdir -p ${CMAKE_BINARY_DIR}/build
         ${srcdir}/inline.sh kBrowsePy < ${srcdir}/browse.py > ${CMAKE_BINARY_DIR}/build/browse_py.h\n")
        set(browser 1)
    endif()
    if(browser)
        message(STATUS "Building dependency browser")
        message(STATUS)
        add_definitions(-DNINJA_PYTHON="${PYTHON_EXECUTABLE}")
        list(APPEND ninja_lib ${srcdir}/browse.cc)
        execute_process(COMMAND sh ${inline})
        include_directories(${CMAKE_BINARY_DIR}/build)
    endif()
endif()

# find all headers
file(GLOB ninja_h ${srcdir}/*.h)



#
# ninja library, needed by 'ninja' and tests
#
add_library(ninja_lib STATIC ${ninja_lib} ${ninja_h})
set_target_properties(ninja_lib PROPERTIES  COMPILE_FLAGS ${nortti})


#
# ninja executable
#
add_executable(ninja ${srcdir}/ninja.cc)
set_target_properties(ninja PROPERTIES COMPILE_FLAGS ${nortti})
target_link_libraries(ninja ninja_lib ${platformlibs})
install_targets(/bin ninja)

#
# cldeps executable
#
if(windows OR cross)
    include_directories(${srcdir})
    add_executable(cldeps ${srcdir}/../misc/cldeps.cc)
    set_target_properties(cldeps PROPERTIES COMPILE_FLAGS ${nortti})
    target_link_libraries(cldeps ninja_lib ${platformlibs})
    install_targets(/bin cldeps)
endif()


#
# Tests, gtest should be the path to gtest sources
#
if(gtest)
    enable_testing()

    macro(set_test_cflags target)
        set(tflags "-DGTEST_HAS_PTHREAD=0 -I${gtest}/include")
        set_target_properties(${target} PROPERTIES COMPILE_FLAGS "${tflags} -I${gtest}/include ${ARGN}")
    endmacro()

    # build gtest, test.cc only once
    add_library(testlib ${srcdir}/test.cc ${gtest}/src/gtest-all.cc ${gtest}/src/gtest_main.cc)
    set_test_cflags(testlib -I${gtest})

    # building a test is always the same
    macro(add_ninja_test test_name)
        set(test_sources ${ARGN})
        add_executable(${test_name} ${test_sources} ${ninja_h})
        target_link_libraries(${test_name} testlib ninja_lib ${platformlibs})
        set_test_cflags(${test_name})
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
            set(test_name tst_${file_name})
            add_ninja_test(${test_name} ${it})
            add_test(NAME ${test_name} COMMAND ${test_name})
        endforeach()
    endif()

    # not added as test because it breaks 'make test'
    add_ninja_test(parser_perftest ${srcdir}/parser_perftest.cc)
endif()

