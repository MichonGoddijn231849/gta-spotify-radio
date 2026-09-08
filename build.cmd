@echo off
setlocal

set "BUILD_DIR=%~1"
if not defined BUILD_DIR set "BUILD_DIR=build"

for %%E in (BuildTools Community Professional Enterprise) do (
  if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\%%E\Common7\Tools\VsDevCmd.bat" (
    set "VS_ROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\%%E"
  )
)

if defined VS_ROOT (
  call "%VS_ROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
  if errorlevel 1 exit /b %errorlevel%
)

where cmake >nul 2>nul
if not errorlevel 1 set "CMAKE_EXE=cmake"

if not defined CMAKE_EXE (
  if defined VS_ROOT set "CMAKE_EXE=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)

if not defined CMAKE_EXE (
  echo CMake was not found. Install Visual Studio 2022 with Desktop development with C++.
  exit /b 1
)

where ninja >nul 2>nul
if not errorlevel 1 set "NINJA_EXE=ninja"
if not defined NINJA_EXE (
  if defined VS_ROOT set "NINJA_EXE=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
)

if not defined NINJA_EXE (
  echo Ninja was not found. Install Visual Studio's CMake tools for Windows component.
  exit /b 1
)

"%CMAKE_EXE%" -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%"
if errorlevel 1 exit /b %errorlevel%
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release
exit /b %errorlevel%
