@echo off
rem Offline fixtures only: in-memory objects and production guidance, no game or installed files.
setlocal
cd /d "%~dp0.."
call tools\msvc-x64-env.cmd
if errorlevel 1 exit /b 1
set VSLANG=1033
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
if errorlevel 1 exit /b 1
cmake --build build --target edf6common
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /Gy /MT /W4 /WX /utf-8 /permissive- /I src /I common /I third_party/EDFModLoader /I build/generated tests\heli_command_test.cpp /Fo:build\heli_command_test.obj /Fe:build\heli_command_test.exe /link /OPT:REF build\common\edf6common.lib user32.lib
if errorlevel 1 exit /b 1
build\heli_command_test.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /Gy /MT /W4 /WX /utf-8 /permissive- /I src /I common /I third_party/EDFModLoader /I build/generated /c src\jet_flight.cpp /Fo:build\jet_command_flight.obj
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /Gy /MT /W4 /WX /utf-8 /permissive- /I src /I common /I third_party/EDFModLoader /I build/generated tests\jet_command_test.cpp /Fo:build\jet_command_test.obj /Fe:build\jet_command_test.exe /link /OPT:REF build\jet_command_flight.obj build\common\edf6common.lib user32.lib
if errorlevel 1 exit /b 1
build\jet_command_test.exe
exit /b %errorlevel%
