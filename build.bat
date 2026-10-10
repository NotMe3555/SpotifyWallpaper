@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set "OUT=%~1"
if not defined OUT set "OUT=."
if not exist "%OUT%" mkdir "%OUT%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio 2022 or the "Build Tools for Visual Studio" with the C++ workload is required.
  exit /b 1
)
for /f "usebackq delims=" %%i in (`call "!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo Couldn't find a Visual Studio install with the C++ tools.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1

set "WV2=third_party\webview2\build\native"
if not exist "%WV2%\include\WebView2.h" (
  echo Downloading the WebView2 SDK...
  if not exist third_party\webview2 mkdir third_party\webview2
  curl.exe -sL -o third_party\webview2\pkg.zip https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.3856.49 || exit /b 1
  tar.exe -xf third_party\webview2\pkg.zip -C third_party\webview2 build/native/include build/native/x64/WebView2LoaderStatic.lib LICENSE.txt || exit /b 1
  del third_party\webview2\pkg.zip
)

if not exist build mkdir build
rc /nologo /fo build\app.res src\app.rc || exit /b 1
cl /nologo /std:c++20 /O2 /EHsc /W3 /utf-8 /MD /bigobj /wd4251 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
  /I "%WV2%\include" /Fobuild\ /Fdbuild\ /Fe:"%OUT%\SpotifyWallpaper.exe" ^
  src\main.cpp src\auth.cpp src\server.cpp src\spotify.cpp src\http_client.cpp src\json.cpp src\util.cpp ^
  src\wallpaper.cpp src\settings.cpp src\youtube.cpp src\lyrics.cpp src\audio.cpp ^
  build\app.res ^
  /link /SUBSYSTEM:WINDOWS ^
  user32.lib gdi32.lib shell32.lib ole32.lib advapi32.lib comctl32.lib dwmapi.lib d2d1.lib dwrite.lib ^
  dxguid.lib windowscodecs.lib winhttp.lib ws2_32.lib bcrypt.lib crypt32.lib shlwapi.lib version.lib comdlg32.lib ^
  mmdevapi.lib "%WV2%\x64\WebView2LoaderStatic.lib" || exit /b 1

echo.
echo Built SpotifyWallpaper.exe
