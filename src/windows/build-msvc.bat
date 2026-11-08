@echo off
rem ---------------------------------------------------------------------------
rem Build w7telnet-listen with MSVC (cl.exe) from the free Visual Studio /
rem Build Tools ("Desktop development with C++").
rem
rem   src\windows\build-msvc.bat        build 64-bit -> w7telnet-listen-x64.exe
rem   src\windows\build-msvc.bat x86    build 32-bit -> w7telnet-listen-x86.exe
rem
rem The C runtime is linked statically (/MT), so the exe is self-contained and
rem needs no VC runtime redistributable or UCRT DLLs - it is meant to run on a
rem stock Windows 7.
rem
rem Run from the repository root. VS 2022 is located automatically for the
rem requested architecture; alternatively run from a matching "Native Tools
rem Command Prompt for VS", in which case the cl.exe already in PATH is used.
rem ---------------------------------------------------------------------------
setlocal

set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=x64"
if /i "%ARCH%"=="x64" ( set "VCARCH=64" & set "OUT=w7telnet-listen-x64.exe" & goto detect )
if /i "%ARCH%"=="x86" ( set "VCARCH=32" & set "OUT=w7telnet-listen-x86.exe" & goto detect )
echo Usage: build-msvc.bat [x64^|x86]   (default x64)
exit /b 1

:detect
set "VCVARS="
for %%e in (Community Professional Enterprise BuildTools) do if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars%VCARCH%.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars%VCARCH%.bat"
if defined VCVARS call "%VCVARS%" >nul 2>nul
if defined VCVARS goto build
where cl >nul 2>nul && goto build
echo Could not find the VS 2022 C++ tools for %ARCH%.
echo Install "Desktop development with C++", or run this from the matching
echo "Native Tools Command Prompt for VS".
exit /b 1

:build
cl /nologo /O2 /MT /Isrc /Fe:%OUT% src\windows\telnet-listen.c src\core\telnet.c
if errorlevel 1 (
    del /q telnet-listen.obj telnet.obj >nul 2>nul
    echo Build FAILED.
    exit /b 1
)
del /q telnet-listen.obj telnet.obj >nul 2>nul
echo Done: %OUT%
endlocal
