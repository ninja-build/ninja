find_package(Boost 1.81 REQUIRED)

add_executable(nj
  ../src/ninja.cc
  real_command_runner.cc
  status_printer.cc
)
target_link_libraries(nj PRIVATE libninja libninja-re2c Boost::headers)
target_compile_features(nj PUBLIC cxx_std_23)
target_compile_definitions(nj PRIVATE BOOST_ASIO_DISABLE_THREADS BOOST_PROCESS_USE_STD_FS)
if(WIN32)
  target_compile_definitions(nj PRIVATE _WIN32_WINNT=0x0601 BOOST_ALL_NO_LIB)
endif()
