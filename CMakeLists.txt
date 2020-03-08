cmake_minimum_required(VERSION 3.15)
project(ninja)

# --- optional link-time optimization
if(CMAKE_BUILD_TYPE MATCHES "Release")
	include(CheckIPOSupported)
	check_ipo_supported(RESULT lto_supported OUTPUT error)

	if(lto_supported)
		message(STATUS "IPO / LTO enabled")
		set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
	else()
		message(STATUS "IPO / LTO not supported: <${error}>")
	endif()
endif()

# --- compiler flags
if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	string(APPEND CMAKE_CXX_FLAGS " /W4 /GR- /Zc:__cplusplus")
else()
	include(CheckCXXCompilerFlag)
	check_cxx_compiler_flag(-Wno-deprecated flag_no_deprecated)
	if(flag_no_deprecated)
		string(APPEND CMAKE_CXX_FLAGS " -Wno-deprecated")
	endif()
	check_cxx_compiler_flag(-fdiagnostics-color flag_color_diag)
	if(flag_color_diag)
		string(APPEND CMAKE_CXX_FLAGS " -fdiagnostics-color")
	endif()
endif()

# --- optional re2c
find_program(RE2C re2c)
if(RE2C)
	# the depfile parser and ninja lexers are generated using re2c.
	function(re2c IN OUT)
		add_custom_command(DEPENDS ${IN} OUTPUT ${OUT}
			COMMAND ${RE2C} -b -i --no-generation-date -o ${OUT} ${IN}
		)
	endfunction()
	re2c(${CMAKE_SOURCE_DIR}/src/depfile_parser.in.cc ${CMAKE_BINARY_DIR}/depfile_parser.cc)
	re2c(${CMAKE_SOURCE_DIR}/src/lexer.in.cc ${CMAKE_BINARY_DIR}/lexer.cc)
	add_library(libninja-re2c OBJECT ${CMAKE_BINARY_DIR}/depfile_parser.cc ${CMAKE_BINARY_DIR}/lexer.cc)
else()
	message(WARNING "re2c was not found; changes to src/*.in.cc will not affect your build.")
	add_library(libninja-re2c OBJECT src/depfile_parser.cc src/lexer.cc)
endif()
target_include_directories(libninja-re2c PRIVATE src)

# Core source files all build into ninja library.
add_library(libninja OBJECT
	src/build_log.cc
	src/build.cc
	src/clean.cc
	src/clparser.cc
	src/dyndep.cc
	src/dyndep_parser.cc
	src/debug_flags.cc
	src/deps_log.cc
	src/disk_interface.cc
	src/edit_distance.cc
	src/eval_env.cc
	src/graph.cc
	src/graphviz.cc
	src/line_printer.cc
	src/manifest_parser.cc
	src/metrics.cc
	src/parser.cc
	src/state.cc
	src/string_piece_util.cc
	src/util.cc
	src/version.cc
)
if(WIN32)
	target_sources(libninja PRIVATE
		src/subprocess-win32.cc
		src/includes_normalize-win32.cc
		src/msvc_helper-win32.cc
		src/msvc_helper_main-win32.cc
		src/getopt.c
	)
	if(MSVC)
		target_sources(libninja PRIVATE src/minidump-win32.cc)
	endif()
else()
	target_sources(libninja PRIVATE src/subprocess-posix.cc)
endif()

#Fixes GetActiveProcessorCount on MinGW
if(MINGW)
target_compile_definitions(libninja PRIVATE _WIN32_WINNT=0x0601 __USE_MINGW_ANSI_STDIO=1)
endif()

# Main executable is library plus main() function.
add_executable(ninja src/ninja.cc)
target_link_libraries(ninja PRIVATE libninja libninja-re2c)

# Tests all build into ninja_test executable.
add_executable(ninja_test
	src/build_log_test.cc
	src/build_test.cc
	src/clean_test.cc
	src/clparser_test.cc
	src/depfile_parser_test.cc
	src/deps_log_test.cc
	src/disk_interface_test.cc
	src/dyndep_parser_test.cc
	src/edit_distance_test.cc
	src/graph_test.cc
	src/lexer_test.cc
	src/manifest_parser_test.cc
	src/ninja_test.cc
	src/state_test.cc
	src/string_piece_util_test.cc
	src/subprocess_test.cc
	src/test.cc
	src/util_test.cc
)
if(WIN32)
	target_sources(ninja_test PRIVATE src/includes_normalize_test.cc src/msvc_helper_test.cc)
endif()
target_link_libraries(ninja_test PRIVATE libninja libninja-re2c)

foreach(perftest
  build_log_perftest
  canon_perftest
  clparser_perftest
  depfile_parser_perftest
  hash_collision_bench
  manifest_parser_perftest
)
  add_executable(${perftest} src/${perftest}.cc)
  target_link_libraries(${perftest} PRIVATE libninja libninja-re2c)
endforeach()

enable_testing()
add_test(NinjaTest ninja_test)

install(TARGETS ninja DESTINATION bin)
