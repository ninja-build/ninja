@echo off

setlocal enabledelayedexpansion

echo Building ninja manually...

FOR /F "usebackq tokens=*" %%i in (`dir /s/b src\*.cc src\*.c ^| findstr /v test ^| findstr /v subprocess.cc ^| findstr /v browse.cc`) do set srcs=!srcs! %%i

cl /nologo /Zi /W4 /WX /D_CRT_SECURE_NO_WARNINGS /wd4530 /wd4512 /wd4706 /wd4100 %SRCS% /link /out:ninja.bootstrap.exe
if %ERRORLEVEL% gtr 0 goto dead

echo Building ninja using itself...
python configure.py
ninja.bootstrap.exe ninja
del *.obj *.pdb ninja.bootstrap.exe *.ilk

echo Done!

:dead
