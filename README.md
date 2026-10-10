# Spotify Wallpaper

A Windows live wallpaper that shows the song playing on Spotify, with playback controls, visualizers, synced lyrics, and optional music videos.

## Downloads

- [Portable build](https://github.com/NotMe3555/SpotifyWallpaper/releases/latest/download/SpotifyWallpaper-portable.zip)
- [Windows installer](https://github.com/NotMe3555/SpotifyWallpaper/releases/latest/download/SpotifyWallpaper-setup.exe)
- [All releases](https://github.com/NotMe3555/SpotifyWallpaper/releases)

## Screenshots

<p align="center">
  <img src="assets/screenshots/wallpaper-canyon.png" alt="Spotify Wallpaper with a canyon background and circular visualizer" width="32%">
  <img src="assets/screenshots/wallpaper-anime.png" alt="Spotify Wallpaper with animated artwork and a circular visualizer" width="32%">
  <img src="assets/screenshots/wallpaper-album-art.png" alt="Spotify Wallpaper with album art and an audio visualizer" width="32%">
</p>
<p align="center">
  <img src="assets/screenshots/wallpaper-minimal.png" alt="Minimal Spotify Wallpaper layout with album art and visualizer" width="32%">
  <img src="assets/screenshots/wallpaper-self-care.png" alt="Spotify Wallpaper with a dark music video background" width="32%">
  <img src="assets/screenshots/app-settings.png" alt="Spotify Wallpaper display settings window, centered with transparent margins" width="32%">
</p>

The portable build needs no installation. Extract it to a writable folder and run `SpotifyWallpaper.exe`. The installer sets up a per-user copy and adds Start menu shortcuts.

## Requirements

- Windows 10 or 11, 64-bit
- Microsoft Edge WebView2 Runtime for the live wallpaper (included with Windows 11; install it from [Microsoft](https://developer.microsoft.com/microsoft-edge/webview2/) on Windows 10 if needed)
- A Spotify account and a Spotify developer app
- Spotify Premium for playback controls
- Optional: YouTube Data API key for music videos

## First run

1. Create a Spotify app at the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard).
2. Add `http://127.0.0.1:8888/callback` as its redirect URI and enable the Web API.
3. Open Spotify Wallpaper, enter the app's Client ID and Client Secret on the Account tab, then choose **Connect Spotify**.
4. Approve access in your browser. The app encrypts saved credentials with Windows DPAPI.

Your credentials, Spotify login, preferences, and API caches are stored next to the executable. Keep the application folder writable. These local files are excluded from this repository and its release archives.

## Build from source

Run `build.bat` from a Visual Studio 2022 Developer Command Prompt or a system with Visual Studio 2022 Build Tools and the Desktop development with C++ workload installed. The script downloads the WebView2 SDK on first build.

```bat
build.bat
```

To send the executable and runtime DLLs to a separate output directory:

```bat
build.bat path\to\output
```

The installer source is `installer.iss`; compile it with the Inno Setup compiler after building the app. Tagged releases are also built and published by the GitHub Actions workflow.

## Features

- Album art, track details, progress, clock, playback controls, and volume
- Live wallpaper on one or multiple displays, or use with Lively Wallpaper or Wallpaper Engine
- Custom layout, backgrounds, colors, album art shapes, visualizer, and synced lyrics
- Optional official YouTube music videos, cached locally

Playback controls require Spotify Premium. YouTube videos depend on embed availability and API quota.

## Open source and third-party software

This project is licensed under the MIT License. Bundled third-party components retain their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Spotify, YouTube, Microsoft, and their respective marks belong to their owners. Spotify Wallpaper is an independent project and is not affiliated with Spotify AB.
