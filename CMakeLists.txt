cmake_minimum_required(VERSION 3.12)
project(ninja)

# Core source files all build into ninja library.
list(APPEND libninja_SOURCES
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
	list(APPEND libninja_SOURCES ${CMAKE_BINARY_DIR}/depfile_parser.cc ${CMAKE_BINARY_DIR}/lexer.cc)
else()
	message(WARNING "re2c was not found; changes to src/*.in.cc will not affect your build.")
	list(APPEND libninja_SOURCES src/depfile_parser.cc src/lexer.cc)
endif()


if(WIN32)
	list(APPEND libninja_SOURCES
		src/subprocess-win32.cc
		src/includes_normalize-win32.cc
		src/msvc_helper-win32.cc
		src/msvc_helper_main-win32.cc
		src/getopt.c
	)
	if(MSVC)
                list(APPEND libninja_SOURCES src/minidump-win32.cc)
	endif()
else()
        list(APPEND libninja_SOURCES src/subprocess-posix.cc)
endif()

add_library(libninja OBJECT ${libninja_SOURCES})

target_include_directories(libninja PRIVATE src)

target_compile_options(libninja PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4 /GR- /Zc:__cplusplus,-Wno-deprecated -fdiagnostics-color>)

#Fixes GetActiveProcessorCount on MinGW
target_compile_definitions(libninja PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_WIN32_WINNT=0x0601>)

# Main executable is library plus main() function.
add_executable(ninja src/ninja.cc)
target_link_libraries(ninja PRIVATE libninja)

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
target_link_libraries(ninja_test PRIVATE libninja)

enable_testing()
add_test(NinjaTest ninja_test)
