# Windows development and audio paths

Use the Now Playing transport button or Space to play, pause, and resume a local WAV. Pausing keeps your place and the selected output. Stop ends playback. Next advances the library selection.

## Supported local build setup

Validated on Windows 11 x64 with Visual Studio 2022 Build Tools 17.14, MSVC 19.44 (v143), Windows SDK 10.0.26100.0, and CMake 3.31.6 bundled with Visual Studio. The project has no third-party C++ library dependency. Git is optional for the build. Flutter/Dart and Android tools are not needed for these targets.

Install Visual Studio 2022 Build Tools or Visual Studio Community through Visual Studio Installer and select:

* **Desktop development with C++** workload.
* **MSVC v143 x64/x86 build tools**.
* **Windows 11 SDK (10.0.26100.0)**.
* **C++ CMake tools for Windows**.

Open **Developer PowerShell for VS 2022** (or a VS 2022 x64 Native Tools prompt) so MSVC, Windows SDK headers/libraries, MSBuild, and CMake are on `PATH`. Confirm with:

```powershell
cl
cmake --version
where.exe cmake
```

Configure, build, and run tests from the repository root:

```powershell
cmake -S . -B build/windows-x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/windows-x64 --config Release --clean-first --parallel 4
ctest --test-dir build/windows-x64 -C Release --output-on-failure
```

The CMake target builds the portable DSP library and tests on other C++20 platforms too. The WASAPI and Win32 UI targets are Windows-only. The redesigned native UI uses GDI+ plus the C++/WinRT headers and `windowsapp.lib` shipped in the Windows SDK; there is no NuGet or other third-party runtime dependency.

## Desktop application

Run the redesigned native Windows UI:

```powershell
build\windows-x64\Release\sonicsync_gui.exe
```

The dark native app has Now Playing, Equalizer, Local Library, and Devices pages. Add WAV files on the Library page or Now Playing panel, choose an output endpoint, and press Play. EQ profile chips and bass/mid/treble sliders update at the next render buffer while playback continues; the DSP smooths filter changes. Bypass crossfades to the unprocessed WAV stream over about 15 ms. The Now Playing spectrum is computed from post-DSP samples in the render process. The app lists active Windows endpoints at startup and selects the current default. Windows GSMTC supplies track, artist, album, source app, and playback state when exposed by a media session; this is metadata only. It does not capture or route Spotify, YouTube Music, browsers, or other application audio through SonicSync. Album artwork is requested from GSMTC thumbnails when available, but was not live-tested. Page Up and Page Down navigate long libraries. Playback advances to the next WAV when a track finishes and stops at the end. The Devices page can refresh available outputs after connecting or disconnecting hardware.

## App-owned WAV playback (direct output and EQ bypass)

List active Windows render endpoints:

```powershell
build\windows-x64\Release\sonicsync_wasapi.exe --list
```

Play a supported mono/stereo PCM 8/16/24/32-bit or float32 WAV through the selected endpoint:

```powershell
build\windows-x64\Release\sonicsync_player.exe .\examples\test-tone.wav 0 --preset hip-hop
```

The file is read before playback. The player renders only its own file stream to the selected output using shared-mode WASAPI, which converts the client stream format to the endpoint mix format. EQ runs on preallocated blocks on the event-driven render thread. To compare the unchanged input path, run:

```powershell
build\windows-x64\Release\sonicsync_player.exe .\examples\test-tone.wav 0 --bypass
```

The bypass sends the WAV directly to the same selected endpoint, without the DSP. Supported built-in preset ids are `neutral`, `music`, `hip-hop`, `rnb`, `classical`, `acoustic`, `rock`, `movies`, `gaming`, and `voice`. The player is a console WAV player, not yet a general desktop audio app; it does not capture another application's audio.

The player and DSP executable build has been verified. A near-silent 6 s 100 Hz probe was rendered to a real WASAPI endpoint while a named control mapping changed preset from neutral to Hip-hop and toggled bypass on then off. The player reported `Submitted 288000 / 288000 frames`, one live preset update, final preset id `2` (Hip-hop), and bypass off. This verifies sample processing and a live control update without restarting the player, not subjective audio quality or measured latency. Closing the process closes its WASAPI stream.

## Loopback bridge (explicit relay only)

```powershell
build\windows-x64\Release\sonicsync_wasapi.exe --run 0 1
```

This captures loopback from one render endpoint and renders to a different endpoint. The source application continues playing on its original endpoint, so the unprocessed source may remain audible. It is not transparent system-wide processing; do not use two audible endpoints if duplicate audio is undesirable. The bridge uses a bounded preallocated queue and MMCSS “Pro Audio” priority. Endpoint enumeration and executable build are verified; live transfer, latency, and disconnect recovery are not.

## System-wide processing boundary

WASAPI loopback does not transparently intercept and replace every application's output. A system-wide Windows route would require a properly packaged endpoint APO or virtual audio endpoint and the associated signing, installation, compatibility, and recovery work. SonicSync does not bundle or install a driver/APO.


## External application routing

The Devices page can start the shared-mode WASAPI loopback route from a selected render endpoint to a different physical output, with the same DSP, live controls, bypass, and analyzer. External apps must be manually directed to a separate virtual playback endpoint. SonicSync does not set the Windows default device. No virtual endpoint is currently installed on the development host, so external app playback is not verified. See [Windows external audio setup and recovery](../../docs/windows-external-audio.md).

## Optional Android media remote

The separately launched HTTPS remote host reports and controls the current Windows GSMTC media session. See [Android to Windows remote control](../../docs/windows-remote-control.md). It is opt-in, does not start with the GUI, and does not alter audio routing. It requires Node.js 20+ and the `sonicsync_media_session_remote.exe` build target.

## Brand assets and Windows identity

The Obsidian & Gold palette uses #080808, #141414, #1C1C1C, #F5F5F5, #969696, and #D4AF70. The original audio-wave S master and wordmark live in `assets/brand/sonicsync-mark.svg` and `assets/brand/sonicsync-lockup.svg`. `sonicsync.ico` contains 16, 24, 32, 48, 64, 128, and 256px PNG-backed icon frames; `sonicsync-mark-1024.png` is the high-resolution raster export. Recreate PNG/ICO exports on Windows with `powershell -ExecutionPolicy Bypass -File assets/brand/render-brand-assets.ps1`.

The Win32 resource embeds the executable icon and product version metadata. The UI assigns large/small window icons and the `SonicSync.Audio` AppUserModelID so the taskbar uses a consistent SonicSync identity. Release builds use the static MSVC runtime, so the package does not depend on a separately installed Visual C++ Redistributable. Windows system components (GSMTC, WIC, WASAPI, GDI+) are supplied by supported Windows versions.

## Build and install a release

From Developer PowerShell for VS 2022:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\packaging\windows\Build-Release.ps1
```

This performs an x64 Release build, runs CTest, stages the five application executables and remote-host files, and writes `artifacts\windows\SonicSync-1.0.2-win-x64.zip`. Extract the bundle and run `Install-SonicSync.ps1`. It installs under `%LOCALAPPDATA%\Programs\SonicSync`, registers the app under the current user's Installed Apps registry key, and creates desktop and Start Menu shortcuts. The Start Menu shortcut makes SonicSync discoverable by Windows Search once that folder is indexed. Index refresh may take a short time; use the Start Menu folder if search is initially empty. No administrator rights, service, driver, APO, virtual endpoint, or audio-route change is involved. Remove it through Settings > Apps > Installed apps or `Uninstall-SonicSync.ps1`; uninstall removes only known program files and shortcuts, leaving any data folders outside the app folder intact. SonicSync saves library paths, track selection, EQ profile, tone controls and bypass when the studio closes. To reset them, close the studio and delete `%LOCALAPPDATA%\SonicSync\studio-session.txt`. The next launch starts with playback stopped.

The installer is a PowerShell per-user package rather than Inno Setup/MSI because an external installer compiler is not required by this project and was not available on the development host. Its install and uninstall scripts can be exercised in a throwaway directory using `-InstallDirectory`, `-DesktopDirectory`, `-StartMenuDirectory`, `-RegistryKeyPath`, and `-NoRegistry`. Keep the archive extraction directory until installation finishes; shortcuts launch the installed executable, not the extracted build.

## Media artwork behavior

The UI requests thumbnails from the current Windows Global System Media Transport Controls session. Thumbnail bytes are limited to 8 MiB, decoded and center-cropped through Windows Imaging Component to 512×512 BGRA on the background metadata thread, and then drawn from immutable cached pixels. Media-session and media-properties notifications cancel a stale in-flight update; a missing thumbnail is retried at a bounded interval and the branded S mark is shown if data is absent, unreadable, or unsupported. This does not imply that SonicSync processes the session's audio. Player support and artwork availability depend on what that application publishes through GSMTC; Spotify/browser live sessions were not available for verification on this build host.
