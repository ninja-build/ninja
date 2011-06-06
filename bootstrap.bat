rem This must be run inside a visual studio command prompt

echo Building ninja manually...

cl /EHsc /MD /D WIN32 /D NOMINMAX /D NDEBUG /D _CONSOLE ^
src/build.cc src/build_log.cc src/clean.cc ^
src/eval_env.cc src/graph.cc src/graphviz.cc src/ninja.cc src/ninja_jumble.cc ^
src/parsers.cc src/stat_cache.cc src/subprocess-win32.cc src/util.cc src/getopt.c /link /out:ninja-bootstrap.exe

rem This must be run on a machine with Python installed and .py files pointed at it.

configure.py --platform=windows
ninja-bootstrap  ninja
del ninja-bootstrap.exe