@echo off
rem ---------------------------------------------------------------------------
rem  FileMender / 文件属性修改器  -  one click build (pure C, MinGW-w64)
rem  Output: FileMender.exe (single file, no runtime DLLs required)
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion
cd /d "%~dp0"

set CC=gcc
set RC=windres
where %CC% >nul 2>nul
if errorlevel 1 set CC="D:\Program Files\gcc-15.2.0\bin\gcc.exe"
where %RC% >nul 2>nul
if errorlevel 1 set RC="D:\Program Files\gcc-15.2.0\bin\windres.exe"

if not exist build mkdir build

echo [1/3] resources ...
%RC% --codepage=65001 -I res res\app.rc -O coff -o build\app.res
if errorlevel 1 goto :fail

echo [2/3] compile + link ...
%CC% -std=c11 -Os -s -municode -mwindows ^
  -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 ^
  -finput-charset=UTF-8 -fexec-charset=UTF-8 ^
  -Wall -Wextra -Wl,--gc-sections ^
  src\main.c src\ui.c src\fileops.c src\util.c src\hash.c build\app.res ^
  -o FileMender.exe ^
  -lgdi32 -luser32 -lcomdlg32 -lshell32 -lcomctl32 -lole32 -luuid -static
if errorlevel 1 goto :fail

echo [3/3] done.
for %%I in (FileMender.exe) do echo     %%~fI  (%%~zI bytes)
exit /b 0

:fail
echo BUILD FAILED
exit /b 1
