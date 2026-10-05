@echo off
rem Zero Browser self-built build script
rem Transport: WinHTTP (system TLS). Media: Media Foundation + WASAPI.
rem Engine: self-built HTML/CSS/layout/paint. Console subsystem + FreeConsole.
setlocal
cd /d "%~dp0"

if not exist build mkdir build

g++ -std=c++17 -O2 -Wall -Wextra ^
  src\main.cpp ^
  src\app.cpp ^
  src\engine.cpp ^
  src\gdi.cpp ^
  src\html.cpp ^
  src\image.cpp ^
  src\network.cpp ^
  src\media.cpp ^
  src\audio_out.cpp ^
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
