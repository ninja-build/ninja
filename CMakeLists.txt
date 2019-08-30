cmake_minimum_required(VERSION 3.1)
project(ninja)

if(MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /GR-")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated -fdiagnostics-color")
endif()

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

enable_testing()
add_test(NinjaTest ninja_test)
