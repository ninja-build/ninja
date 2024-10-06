cmake_minimum_required(VERSION 3.10)

# Set the project name
project(HelloWorld)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add the executable
add_executable(hello_world hello_world.cpp)

# Optional: You can set the value of N here
# target_compile_definitions(hello_world PRIVATE N="CMake")