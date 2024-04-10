find_package(Boost 1.84 REQUIRED)
find_package(Threads REQUIRED)

add_executable(nj
  ../src/ninja.cc
  real_command_runner.cc
  status_printer.cc
)
target_link_libraries(nj PRIVATE libninja libninja-re2c Boost::headers Threads::Threads)
target_compile_features(nj PUBLIC cxx_std_23)
