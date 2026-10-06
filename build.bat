@echo off
rem Zero Browser self-built build script.
rem
rem Produces BOTH architectures from the same sources:
rem   build\zero-browser.exe       32-bit (x86)
rem   build\zero-browser-x64.exe   64-bit (x64)
rem
rem Transport: WinHTTP (system TLS). Media: Media Foundation + WASAPI.
rem Engine: self-built HTML/CSS/layout/paint plus a self-built JS interpreter.
rem Console subsystem + FreeConsole. Static linking: single portable exe with no
rem MinGW runtime DLLs.
rem Stack: 8 MB. JS recursion is C++ recursion (one JS call = several C++ frames),
rem so the default 1 MB main-thread stack overflows long before the interpreter's
rem own call-depth guard trips.
setlocal enabledelayedexpansion
cd /d "%~dp0"

if not exist build mkdir build

rem A browser left running holds the output file, and the link then fails with
rem "cannot open output file ...: Permission denied". Kill stale instances first.
taskkill /IM zero-browser.exe /F >nul 2>&1
taskkill /IM zero-browser-x64.exe /F >nul 2>&1

rem ---- Toolchain: prefer g++ on PATH, otherwise add a known MinGW install ----
where g++ >nul 2>&1 && goto :have_gpp
for %%D in (
    "C:\Program Files (x86)\Embarcadero\Dev-Cpp\TDM-GCC-64\bin"
    "D:\Dev-Cpp\MinGW32\bin"
    "C:\Dev-Cpp\MinGW32\bin"
    "C:\MinGW\bin"
) do (
    if exist "%%~D\g++.exe" set "PATH=%%~D;!PATH!"
)
:have_gpp
where g++ >nul 2>&1 || (
    echo [error] g++ not found. Add your MinGW bin directory to PATH.
    exit /b 1
)

set "SDKROOT=%ProgramFiles(x86)%\Windows Kits\10\Lib"

call :build x86 32 "build\zero-browser.exe" || exit /b 1
call :build x64 64 "build\zero-browser-x64.exe" || exit /b 1

echo.
echo Built: %cd%\build\zero-browser.exe      ^(32-bit^)
echo Built: %cd%\build\zero-browser-x64.exe  ^(64-bit^)
exit /b 0

rem ---------------------------------------------------------------------------
rem %1 = Windows SDK arch dir (x86/x64), %2 = -m bits, %3 = output path
rem ---------------------------------------------------------------------------
:build
set "MFREAD="
for /f "delims=" %%P in ('g++ -m%~2 --print-file-name=libmfreadwrite.a 2^>nul') do set "MFREAD=%%P"
rem g++ echoes the bare name back when the library is missing.
if /i "!MFREAD!"=="libmfreadwrite.a" set "MFREAD="
if not defined MFREAD (
    rem TDM-GCC's multilib ships mfreadwrite.h but no libmfreadwrite.a. The only
    rem export this project uses is MFCreateSourceReaderFromByteStream, so fall
    rem back to the Windows SDK import library for the same DLL (system plumbing).
    for /f "delims=" %%V in ('dir /b /o-n "%SDKROOT%" 2^>nul') do (
        if not defined MFREAD if exist "%SDKROOT%\%%V\um\%~1\mfreadwrite.lib" (
            set "MFREAD=%SDKROOT%\%%V\um\%~1\mfreadwrite.lib"
        )
    )
)
if not defined MFREAD (
    echo [error] neither libmfreadwrite.a nor the Windows SDK mfreadwrite.lib ^(%~1^) was found
    exit /b 1
)

echo Building %~3 ^(m%~2^) ...
g++ -std=c++17 -O2 -Wall -Wextra -m%~2 -D_WIN32_WINNT=0x0601 ^
  -static -static-libgcc -static-libstdc++ ^
  -Wl,--stack,8388608 ^
  src\main.cpp ^
  src\app.cpp ^
  src\engine.cpp ^
  src\gdi.cpp ^
  src\html.cpp ^
  src\i18n.cpp ^
  src\image.cpp ^
  src\network.cpp ^
  src\media.cpp ^
  src\audio_out.cpp ^
  src\js.cpp ^
  src\js_eval.cpp ^
  src\js_builtins.cpp ^
  src\js_globals.cpp ^
  src\js_dom.cpp ^
  -o "%~3" ^
  "!MFREAD!" ^
  -lgdi32 -lwinhttp -lmfplat -lmfuuid ^
  -lole32 -luuid -lstrmiids -lksuser -lavrt ^
  -lwindowscodecs -lmsimg32

if errorlevel 1 (
  echo Build failed: %~3
  exit /b 1
)
echo OK: %~3
exit /b 0
