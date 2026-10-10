@echo off
cd /d "%~dp0"
if not exist SpotifyWallpaper.exe call build.bat || (pause & exit /b 1)
start "" SpotifyWallpaper.exe
