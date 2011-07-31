@echo off
:: Copyright 2011 Google Inc. All Rights Reserved.
::
:: Licensed under the Apache License, Version 2.0 (the "License");
:: you may not use this file except in compliance with the License.
:: You may obtain a copy of the License at
::
::     http://www.apache.org/licenses/LICENSE-2.0
::
:: Unless required by applicable law or agreed to in writing, software
:: distributed under the License is distributed on an "AS IS" BASIS,
:: WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
:: See the License for the specific language governing permissions and
:: limitations under the License.

setlocal enabledelayedexpansion

echo Building ninja manually...

FOR /F "usebackq tokens=*" %%i in (`dir /s/b src\*.cc src\*.c ^| findstr /v test ^| findstr /v subprocess.cc ^| findstr /v browse.cc`) do set srcs=!srcs! %%i

cl /nologo /Zi /W4 /WX /D_CRT_SECURE_NO_WARNINGS /wd4530 /wd4512 /wd4706 /wd4100 %SRCS% /link /out:ninja.bootstrap.exe
if %ERRORLEVEL% gtr 0 goto dead

call misc\makecldeps.bat

echo Building ninja using itself...
python configure.py
ninja.bootstrap.exe ninja.exe
del *.obj *.pdb ninja.bootstrap.exe *.ilk

echo Done!

:dead
