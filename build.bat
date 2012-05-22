call "C:\Programme\Microsoft Visual Studio 8\VC\bin\vcvars32.bat"
mkdir build

cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\ninja.cc /Fobuild\ninja.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\build.cc /Fobuild\build.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\build_log.cc /Fobuild\build_log.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\clean.cc /Fobuild\clean.obj
re2c -b -i --no-generation-date -o src\depfile_parser.cc src\depfile_parser.in.cc
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\depfile_parser.cc /Fobuild\depfile_parser.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\disk_interface.cc /Fobuild\disk_interface.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\edit_distance.cc /Fobuild\edit_distance.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\eval_env.cc /Fobuild\eval_env.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\explain.cc /Fobuild\explain.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\graph.cc /Fobuild\graph.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\graphviz.cc /Fobuild\graphviz.obj
re2c -b -i --no-generation-date -o src\lexer.cc src\lexer.in.cc
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\lexer.cc /Fobuild\lexer.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\metrics.cc /Fobuild\metrics.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\parsers.cc /Fobuild\parsers.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\state.cc /Fobuild\state.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\util.cc /Fobuild\util.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\subprocess-win32.cc /Fobuild\subprocess-win32.obj
cl /nologo /Zi /W4 /WX /wd4530 /wd4100 /wd4706 /wd4512 /wd4800 /wd4702 /wd4819 /GR- /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DNINJA_PYTHON="python.exe" /Ox /DNDEBUG /GL -c src\getopt.c /Fobuild\getopt.obj
lib /nologo /ltcg /out:build\ninja.lib build\build.obj build\build_log.obj build\clean.obj build\depfile_parser.obj build\disk_interface.obj build\edit_distance.obj build\eval_env.obj build\explain.obj build\graph.obj build\graphviz.obj build\lexer.obj build\metrics.obj build\parsers.obj build\state.obj build\util.obj build\subprocess-win32.obj build\getopt.obj
cl build\ninja.obj ninja.lib /nologo /link /DEBUG /libpath:build /LTCG /OPT:REF /OPT:ICF /out:ninja.exe
