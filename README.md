# Spotify Wallpaper

A Windows live wallpaper that shows the song playing on Spotify, with playback controls, visualizers, synced lyrics, and optional music videos. It also includes an optional Discord bot.

## Downloads

- [Portable build](https://github.com/NotMe3555/SpotifyWallpaper/releases/latest/download/SpotifyWallpaper-portable.zip)
- [Windows installer](https://github.com/NotMe3555/SpotifyWallpaper/releases/latest/download/SpotifyWallpaper-setup.exe)
- [All releases](https://github.com/NotMe3555/SpotifyWallpaper/releases)

The portable build needs no installation. Extract it to a writable folder and run `SpotifyWallpaper.exe`. The installer sets up a per-user copy and adds Start menu shortcuts.

## Requirements

- Windows 10 or 11, 64-bit
- Microsoft Edge WebView2 Runtime for the live wallpaper (included with Windows 11; install it from [Microsoft](https://developer.microsoft.com/microsoft-edge/webview2/) on Windows 10 if needed)
- A Spotify account and a Spotify developer app
- Spotify Premium for playback controls
- Optional: YouTube Data API key for music videos; Discord bot application for the Discord tab

## First run

1. Create a Spotify app at the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard).
2. Add `http://127.0.0.1:8888/callback` as its redirect URI and enable the Web API.
3. Open Spotify Wallpaper, enter the app's Client ID and Client Secret on the Account tab, then choose **Connect Spotify**.
4. Approve access in your browser. The app encrypts saved credentials with Windows DPAPI.

Your credentials, Spotify login, preferences, and API caches are stored next to the executable. Keep the application folder writable. These local files are excluded from this repository and its release archives.

## Build from source

Run `build.bat` from a Visual Studio 2022 Developer Command Prompt or a system with Visual Studio 2022 Build Tools and the Desktop development with C++ workload installed. The script downloads the WebView2 SDK and D++ libraries on first build.

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
- Optional Discord bot presence and voice streaming

Playback controls require Spotify Premium. YouTube videos depend on embed availability and API quota. Streaming Spotify audio to other people may violate Spotify's terms; Spotify's Listen Along feature is its supported option for listening with friends.

## Open source and third-party software

This project is licensed under the MIT License. Bundled third-party components retain their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Spotify, Discord, YouTube, Microsoft, and their respective marks belong to their owners. Spotify Wallpaper is an independent project and is not affiliated with Spotify AB.
