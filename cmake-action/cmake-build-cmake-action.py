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
