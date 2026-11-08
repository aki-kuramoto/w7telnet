@echo off
rem ---------------------------------------------------------------------------
rem Build w7telnet-listen on Windows WITHOUT make, using MSYS2 / mingw-w64 gcc.
rem
rem   src\windows\build.bat          build 64-bit  -> w7telnet-listen-x64.exe
rem   src\windows\build.bat x86      build 32-bit  -> w7telnet-listen-x86.exe
rem
rem CRT-free build (-ffreestanding / -nostdlib / -e mainCRTStartup): the exe
rem depends only on KERNEL32.dll and WS2_32.dll, so it runs on a stock
rem Windows 7. (MSVC users: use build-msvc.bat instead.)
rem
rem Needs the matching MSYS2 toolchain installed:
rem   pacman -S mingw-w64-x86_64-gcc   (for x64)
rem   pacman -S mingw-w64-i686-gcc     (for x86)
rem Set MSYS2_ROOT if MSYS2 is not at C:\msys64.
rem ---------------------------------------------------------------------------
setlocal

set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=x64"
if /i "%ARCH%"=="x64" goto arch_x64
if /i "%ARCH%"=="x86" goto arch_x86
echo Usage: build.bat [x64^|x86]   (default x64)
exit /b 1

:arch_x64
set "MDIR=mingw64"
set "OUT=w7telnet-listen-x64.exe"
goto find_gcc

:arch_x86
set "MDIR=mingw32"
set "OUT=w7telnet-listen-x86.exe"
goto find_gcc

:find_gcc
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set "GCCDIR=%MSYS2_ROOT%\%MDIR%\bin"
set "GCC=%GCCDIR%\gcc.exe"
if not exist "%GCC%" goto path_gcc
rem gcc must find cc1.exe, ld.exe and its own DLLs next to it.
rem (Set PATH outside any ( ) block: PATH may contain "(x86)".)
set "PATH=%GCCDIR%;%PATH%"
goto do_build
:path_gcc
set "GCC=gcc"
:do_build
"%GCC%" -O2 -s -ffreestanding -fno-builtin -fno-stack-protector -fno-tree-loop-distribute-patterns -nostdlib -e mainCRTStartup -Isrc -o %OUT% src\windows\telnet-listen.c src\core\telnet.c -lws2_32 -lkernel32 -lgcc
if errorlevel 1 (
    echo Build FAILED. Is the mingw-w64 gcc for %ARCH% installed?
    exit /b 1
)
echo Done: %OUT%
endlocal
