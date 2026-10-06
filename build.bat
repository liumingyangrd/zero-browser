@echo off
rem Zero Browser self-built build script
rem Transport: WinHTTP (system TLS). Media: Media Foundation + WASAPI.
rem Engine: self-built HTML/CSS/layout/paint. Console subsystem + FreeConsole.
rem Static linking: the output is a single portable exe with no MinGW runtime DLLs.
rem Stack: 8 MB. JS recursion is C++ recursion (one JS call = several C++ frames),
rem so the default 1 MB main-thread stack overflows long before the interpreter's
rem own call-depth guard trips.
setlocal
cd /d "%~dp0"

if not exist build mkdir build

g++ -std=c++17 -O2 -Wall -Wextra ^
  -static -static-libgcc -static-libstdc++ ^
  -Wl,--stack,8388608 ^
  src\main.cpp ^
  src\app.cpp ^
  src\engine.cpp ^
  src\gdi.cpp ^
  src\html.cpp ^
  src\image.cpp ^
  src\network.cpp ^
  src\media.cpp ^
  src\audio_out.cpp ^
  src\js.cpp ^
  src\js_eval.cpp ^
  src\js_builtins.cpp ^
  src\js_globals.cpp ^
  src\js_dom.cpp ^
  -o build\zero-browser.exe ^
  -lgdi32 -lwinhttp -lmfplat -lmfreadwrite -lmfuuid ^
  -lole32 -luuid -lmmdevapi -lstrmiids -lksuser -lavrt ^
  -lwindowscodecs -lmsimg32

if errorlevel 1 (
  echo Build failed.
  exit /b 1
)
echo.
echo Built: %cd%\build\zero-browser.exe
