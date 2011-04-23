rem commented out as I don't know enough .bat voodoo to make it do
rem "create dir if it doesn't exist" relibaly

rem rmdir /q /s Bootstrap && mkdir Bootstrap

cl.exe /nologo /EHsc /Zi /I. /Isrc /DWIN32 /DNDEBUG /D_CONSOLE /MD ^
  /Fo"Bootstrap\\" /W3 /Feninja-bootstrap.exe ^
  src\build.cc ^
  src\build_log.cc ^
  src\eval_env.cc ^
  src\graph.cc ^
  src\graphviz.cc ^
  src\parsers.cc ^
  src\subprocess.win32.cpp ^
  src\util.cc ^
  src\ninja_jumble.cc ^
  src\ninja.cc ^
  src\getopt.c ^
  src\clean.cc ^
  src\touch.cc ^
  /link /incremental:no /subsystem:Console /opt:ref /opt:icf shlwapi.lib ^
  && .\ninja-bootstrap.exe -f build-win32.ninja ninja.exe ^
  && del .\ninja-bootstrap.exe
