@echo off

rem This script builds ninja.exe and ninja_test.exe
rem
rem Assumptions
rem 1. vcvars.bat has been run to set up the environment
rem 2. INCLUDE and LIB variables contain path to gtest headers and libs

set CL_OPTIONS=/EHsc /GF /GL /MT /Ox /D WIN32 /D NOMINMAX /D NDEBUG /D _CONSOLE
set LINK_OPTIONS=/LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:CONSOLE /NXCOMPAT /DEBUG

set NINJA_MAIN=src/ninja.cc
set NINJA_FILES=src/build.cc src/build_log.cc src/clean.cc src/eval_env.cc ^
    src/disk_interface.cc src/graph.cc src/graphviz.cc src/parsers.cc src/state.cc ^
    src/subprocess-win32.cc src/util.cc src/getopt.c ^
    src/depfile_parser.cc src/edit_distance.cc
set NINJA_TEST_FILES=%NINJA_FILES% src/build_test.cc src/build_log_test.cc ^
    src/disk_interface_test.cc src/eval_env_test.cc ^
    src/graph_test.cc src/parsers_test.cc src/state_test.cc src/subprocess_test.cc ^
    src/util_test.cc src/clean_test.cc src/test.cc
set GTEST_LIBS=gtest.lib gtest_main.lib

echo Building ninja manually...
cl %CL_OPTIONS% %NINJA_MAIN% %NINJA_FILES% /link /out:ninja.exe %LINK_OPTIONS%

if ERRORLEVEL 1 (
    echo Build failed
    exit /b 1
) else (
    echo Build succeeded.
)

echo Building ninja tests...
cl %CL_OPTIONS% %NINJA_TEST_FILES% /link /out:ninja_test.exe %LINK_OPTIONS% %GTEST_LIBS%

if ERRORLEVEL 1 (
    echo Build failed
    exit /b 1
) else (
    echo Build succeeded. Use ninja_test.exe to run the unit tests.
)

rem Exit here as configure.py not yet customised for Windows platform
exit /b 0

rename ninja-bootstrap.exe ninja.exe

rem This is what we'd do if configure was customised for Windows
rem This must be run on a machine with Python installed and .py files pointed at it.
configure.py --platform=windows
ninja-bootstrap ninja
