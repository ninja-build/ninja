@echo off
pushd %~dp0
echo Building cldeps.exe...
cl /nologo /Ox /Zi /W4 /WX /wd4530 /D_CRT_SECURE_NO_WARNINGS cldeps.cc ..\src\subprocess-win32.cc ..\src\util.cc /I..\src /link /out:..\cldeps.exe
popd
