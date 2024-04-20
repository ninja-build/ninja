find_package(Boost 1.81 REQUIRED)
find_package(Threads REQUIRED)

add_executable(nj
  ../src/ninja.cc
  real_command_runner.cc
  status_printer.cc
)
target_link_libraries(nj PRIVATE libninja libninja-re2c Boost::headers Threads::Threads)
target_compile_features(nj PUBLIC cxx_std_23)
if(WIN32)
  target_compile_definitions(nj PRIVATE _WIN32_WINNT=0x0601 BOOST_ALL_NO_LIB)
endif()
