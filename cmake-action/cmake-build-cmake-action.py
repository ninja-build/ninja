CMakeLists.txt.jsindexnext |previous | CMake » 
latest release (3.27.4)
 Documentation » cmake-variables(7) » CMAKE_PROJECT_NAME
CMAKE_PROJECT_NAME
The name of the top level project.

This variable holds the name of the project as specified in the top level CMakeLists.txt file by a project() command. In the event that the top level CMakeLists.txt contains multiple project() calls, the most recently called one from that top level CMakeLists.txt will determine the name that CMAKE_PROJECT_NAME contains. For example, consider the following top level CMakeLists.txt:

cmake_minimum_required(VERSION 3.0)
project(First)
project(Second)
add_subdirectory(sub)
project(Third)
And sub/CMakeLists.txt with the following contents:

project(SubProj)
message("CMAKE_PROJECT_NAME = ${CMAKE_PROJECT_NAME}")
The most recently seen project() command from the top level CMakeLists.txt would be project(Second), so this will print:

CMAKE_PROJECT_NAME = Second
To obtain the name from the most recent call to project() in the current directory scope or above, see the PROJECT_NAME variable.

Previous topic
CMAKE_PROJECT_HOMEPAGE_URL

Next topic
CMAKE_PROJECT_VERSION

This Page
Show Source
Quick search
indexnext |previous | CMake » 
latest release (3.27.4)
 Documentation » cmake-variables(7) » CMAKE_PROJECT_NAME
© Copyright 2000-2023 Kitware, Inc. and Contributors. Created using Sphinx 5.3.0.Synopsis¶
project(<PROJECT-NAME> [<language-name>...])
project(<PROJECT-NAME>
        [VERSION <major>[.<minor>[.<patch>[.<tweak>]]]]
        [DESCRIPTION <project-description-string>]
        [HOMEPAGE_URL <url-string>]
        [LANGUAGES <language-name>...])
Sets the name of the project, and stores it in the variable PROJECT_NAME. When called from the top-level CMakeLists.txt also stores the project name in the variable CMAKE_PROJECT_NAME.

Also sets the variables:

PROJECT_SOURCE_DIR, <PROJECT-NAME>_SOURCE_DIR
Absolute path to the source directory for the project.

PROJECT_BINARY_DIR, <PROJECT-NAME>_BINARY_DIR
Absolute path to the binary directory for the project.

PROJECT_IS_TOP_LEVEL, <PROJECT-NAME>_IS_TOP_LEVEL
New in version 3.21.

Boolean value indicating whether the project is top-level.

Further variables are set by the optional arguments described in the following. If any of these arguments is not used, then the corresponding variables are set to the empty string.

Options
The options are:

VERSION <version>
Optional; may not be used unless policy CMP0048 is set to NEW.

Takes a <version> argument composed of non-negative integer components, i.e. <major>[.<minor>[.<patch>[.<tweak>]]], and sets the variables

PROJECT_VERSION, <PROJECT-NAME>_VERSION

PROJECT_VERSION_MAJOR, <PROJECT-NAME>_VERSION_MAJOR

PROJECT_VERSION_MINOR, <PROJECT-NAME>_VERSION_MINOR

PROJECT_VERSION_PATCH, <PROJECT-NAME>_VERSION_PATCH

PROJECT_VERSION_TWEAK, <PROJECT-NAME>_VERSION_TWEAK.Introduction
A CMake Generator is responsible for writing the input files for a native build system. Exactly one of the CMake Generators must be selected for a build tree to determine what native build system is to be used. Optionally one of the Extra Generators may be selected as a variant of some of the Command-Line Build Tool Generators to produce project files for an auxiliary IDE.

CMake Generators are platform-specific so each may be available only on certain platforms. The cmake(1) command-line tool --help output lists available generators on the current platform. Use its -G option to specify the generator for a new build tree. The cmake-gui(1) offers interactive selection of a generator when creating a new build tree.

CMake Generators
Command-Line Build Tool Generators
These generators support command-line build tools. In order to use them, one must launch CMake from a command-line prompt whose environment is already configured for the chosen compiler and build tool.

Makefile Generators
Borland Makefiles
MSYS Makefiles
MinGW Makefiles
NMake Makefiles
NMake Makefiles JOM
Unix Makefiles
Watcom WMake
Ninja Generators
Ninja
Ninja Multi-Config
IDE Build Tool Generators
These generators support Integrated Development Environment (IDE) project files. Since the IDEs configure their own environment one may launch CMake from any environment.

Visual Studio Generators
Visual Studio 6
Visual Studio 7
Visual Studio 7 .NET 2003
Visual Studio 8 2005
Visual Studio 9 2008
Visual Studio 10 2010
Visual Studio 11 2012
Visual Studio 12 2013
Visual Studio 14 2015
Visual Studio 15 2017
Visual Studio 16 2019
Visual Studio 17 2022
Other Generators
Green Hills MULTI
Xcode
Extra Generators
Deprecated since version 3.27: Support for "Extra Generators" is deprecated and will be removed from a future version of CMake. IDEs may use the cmake-file-api(7) to view CMake-generated project build trees.

Some of the CMake Generators listed in the cmake(1) command-line tool --help output may have variants that specify an extra generator for an auxiliary IDE tool. Such generator names have the form <extra-generator> - <main-generator>. The following extra generators are known to CMake.

CodeBlocks
CodeLite
Eclipse CDT4
Kate
Sublime Text 2
steps:
- uses: actions/checkout@v2

- name: Configure CMake
  # Configure CMake in a 'build' subdirectory. `CMAKE_BUILD_TYPE` is only required if you are using a single-configuration generator such as make.
  # See https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html?highlight=cmake_build_type
  run: cmake -B ${{github.workspace}}/build -DCMAKE_BUILD_TYPE=${{env.BUILD_TYPE}}

- name: Build
  # Build your program with the given configuration
  run: cmake --build ${{github.workspace}}/build --config ${{env.BUILD_TYPE}}

- name: Test
  working-directory: ${{github.workspace}}/build
  # Execute tests defined by the CMake configuration.  
  # See https://cmake.org/cmake/help/latest/manual/ctest.1.html for more detail
  run: ctest -C ${{env.BUILD_TYPE}}
