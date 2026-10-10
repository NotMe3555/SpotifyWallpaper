# Spotify Wallpaper

A Windows live wallpaper that shows the song playing in the Spotify desktop app, with playback controls, visualizers, synced lyrics, and optional music videos. No Spotify login or developer app is needed.

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
- The Spotify desktop app, running on the same PC
- Optional: YouTube Data API key for music videos

## First run

1. Open Spotify on this PC and play something.
2. Open Spotify Wallpaper. The header shows **Spotify open** once it has found the Spotify app, and the wallpaper starts following it.
3. Optional: paste a YouTube Data API key on the General tab to turn on music videos.

There is nothing to sign in to. Spotify Wallpaper reads the current track, position, and album art that the Spotify app reports to Windows, and sends play, pause, skip, and seek back the same way. It does not use the Spotify Web API, so there are no Spotify rate limits and no Client ID or Client Secret.

Because it follows the Spotify app on this PC, music playing only on a phone or speaker is not shown. The volume slider changes Spotify's level in the Windows volume mixer.

Your preferences, YouTube key (encrypted with Windows DPAPI), and video cache are stored next to the executable. Keep the application folder writable. These local files are excluded from this repository and its release archives.

## Upgrading from an earlier 2.x build

Earlier builds signed in through a Spotify developer app. That is no longer used: the Client ID, Client Secret, and saved login are ignored, and you can delete the Spotify app from the Developer Dashboard. `tokens.json` in the application folder can be deleted. Your YouTube key and preferences carry over.

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
- Your own image, GIF, or MP4 as the background
- Optional official YouTube music videos, cached locally

YouTube videos depend on embed availability and API quota: a key gets 100 video searches a day, and each song is only searched once.

## Open source and third-party software

This project is licensed under the MIT License. Bundled third-party components retain their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Spotify, YouTube, Microsoft, and their respective marks belong to their owners. Spotify Wallpaper is an independent project and is not affiliated with Spotify AB.
