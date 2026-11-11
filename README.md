# SonicSync

SonicSync is a local-first audio enhancement suite. It contains a portable C++20 DSP core, automated DSP tests, a redesigned native Windows audio studio UI, an app-owned WAV player, and a user-selected WASAPI virtual-endpoint relay for external-app audio. The Windows desktop app targets x64.

## Windows and Android playback update (8 November 2026)

- **Windows 1.0.2** adds Pause and Resume to Now Playing and the Space shortcut. The WASAPI player retains its buffered audio and cursor while paused. Stop still ends the track. A live test through the VB-Audio virtual cable paused and resumed a 48,000-frame WAV and submitted all 48,000 frames. Stopping a second player while paused exited cleanly.
- **Android** adds a Now Playing queue where you can jump to or remove an entry. Local tracks remain restorable when mixed with remote streams. Remote URLs stay out of the saved queue. Temporary playback errors keep the item available for retry, and position checkpoints update playback state independently of unchanged track rows.
- **Verification** included six passing Windows CTest cases, the authenticated remote-host loopback smoke test, and a four-page launch/navigation/close smoke test of the packaged studio. All 23 Android JVM tests and 25 device tests passed. On an Android 8.0 (API 26) emulator, the installed app played and paused local WAVs, switched and removed queue entries, reopened with the remaining track paused after a force-stop, and opened Privacy & security. The headless emulator had audio output disabled. It also paired over pinned HTTPS with the packaged Windows host on loopback, read the Windows media-session state, and disconnected with revocation confirmed.

[Windows x64 package](artifacts/windows/1.0.2/SonicSync-1.0.2-win-x64.zip) · [Android debug APK](platforms/android/app/build/outputs/apk/debug/app-debug.apk)

## Current status

| Platform | Current path | Status |
|---|---|---|
| Windows 11 | Native Win32 desktop UI, local WAV playback, Windows media-session metadata, live preset/bypass handoff, WASAPI output | Release targets compile, DSP tests pass, live parameter handoff was exercised during endpoint playback |
| macOS | Core Audio integration planned | Requires macOS/Xcode; system-wide routing requires a virtual device or supported system audio tap |
| Android | Kotlin/Compose Media3 player with persistent local library/queue, M3U/PLS import, Jamendo, Navidrome/Subsonic and Jellyfin provider paths, guarded downloads, and device Equalizer | The debug build includes local queue recovery, a queue editor, and provider playback. Current verification is recorded below. See [Android setup and status](platforms/android/README.md) |
| iOS/iPadOS | In-app AVAudioEngine/Audio Unit processing planned | Requires compatible macOS host and Xcode; cannot EQ arbitrary audio from other apps |

The shared core implements RBJ biquad filters, per-channel filter state, parameter smoothing, a 15 ms wet/dry bypass ramp, immediate-attack/release peak limiting, stereo crossfeed, analysis meters, and preset serialization. The Windows studio provides Now Playing, Equalizer, Local Library, and Devices pages in the Obsidian & Gold design. The Windows player reads a supported WAV file before playback, then processes preallocated audio blocks and renders that app-owned stream directly to one selected endpoint. Preset and bypass changes from the UI reach the render thread over a named shared-memory block and take effect at buffer boundaries; preset coefficients and gain use the DSP's existing transition ramps. The relay can process other apps only when the user explicitly routes them through a separate virtual playback endpoint; it does not silently change Windows routing.

The original SonicSync audio-wave S mark is in `assets/brand/sonicsync-mark.svg`; the lockup is in `assets/brand/sonicsync-lockup.svg`. Production exports include a multi-size Windows ICO and a 1024px PNG mark. Regenerate them on Windows with `powershell -ExecutionPolicy Bypass -File assets/brand/render-brand-assets.ps1`.

## Architecture

* `shared/dsp`: portable real-time-safe DSP with no third-party runtime dependency.
* `shared/protocol`: versioned control message schema and pairing design.
* `platforms/windows`: native Win32 UI and playlist, WASAPI WAV player with direct DSP bypass, endpoint enumeration, and separate loopback-to-render bridge.
* `platforms/macos`: planned Core Audio integration boundary.
* `platforms/android`: Kotlin/Compose player, local library, Media3 service, provider adapters, and supported in-app processing boundary.
* `platforms/ios`: supported in-app processing boundary.

Windows external-audio routing via a virtual endpoint, installation, trade-offs, and recovery are documented in [Windows external audio](docs/windows-external-audio.md). The relay uses WASAPI loopback and does not change the default route. A virtual audio driver is a separate user-installed dependency; no driver/APO is bundled here.

## Build and verify shared DSP

Requirements on Windows: Visual Studio 2022 Build Tools 17.14 or later with MSVC v143 x64, Windows SDK 10.0.26100.0, and CMake 3.20+ (the VS bundled CMake is validated). There are no third-party C++ runtime dependencies. Install the **Desktop development with C++** workload, MSVC v143 x64/x86 tools, Windows 11 SDK 10.0.26100.0, and **C++ CMake tools for Windows**. Use Developer PowerShell for VS 2022.

```powershell
cmake -S . -B build/windows-x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/windows-x64 --config Release --clean-first --parallel 4
ctest --test-dir build/windows-x64 -C Release --output-on-failure
build\windows-x64\Release\dsp_benchmark.exe
```

The benchmark reports the DSP's single-thread throughput as a percentage of real-time work for stereo 128/256/480/960-frame blocks at 44.1/48/96/192 kHz. It is a synthetic engine measurement, not a WASAPI callback-latency or whole-app playback measurement. See [the audit baseline](docs/audit-baseline.md) for the measured baseline and limitations.

The Windows x64 build has been verified with MSVC 19.44, Visual Studio 2022 17.14, CMake 3.31.6, and Windows SDK 10.0.26100. The Android project is under `platforms/android`; its API 37 emulator build, tests, queue restoration, playlist import, and provider limitations are documented in [the Android README](platforms/android/README.md). Flutter/Dart are not selected or required by the C++ DSP and WASAPI targets. See [Windows development details](platforms/windows/README.md).

Launch the desktop app with `build\windows-x64\Release\sonicsync_gui.exe`. It provides Now Playing, metadata, compact live spectrum, response graph, immediately applied profiles, local WAV library, endpoint selection, bypass, and playback controls. GSMTC track/session information and album artwork are obtained independently from playback; the system does not route that media's audio into the DSP. Windows decodes thumbnails with WIC on the metadata worker, then the UI draws a cached 512px square image. Artwork is available only when a media session exposes a readable thumbnail. Streaming-service imports and playback are not implemented. For app-owned audio without the UI, use `build\windows-x64\Release\sonicsync_player.exe <file.wav> <endpoint-index> [--preset music]` or add `--bypass` for direct playback without DSP. For the separate loopback relay, list endpoints with `build\windows-x64\Release\sonicsync_wasapi.exe --list`, then run `build\windows-x64\Release\sonicsync_wasapi.exe --run <capture-index> <render-index>`. The source endpoint may continue playing unprocessed audio. This is not transparent system-wide processing; an APO or virtual endpoint is required for that.

## Windows installation

Build the x64 release bundle using Developer PowerShell for Visual Studio 2022:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\packaging\windows\Build-Release.ps1
```

The script builds Release, runs CTest, stages the installed executables, and creates `artifacts\windows\SonicSync-1.0.2-win-x64.zip`. Extract the archive and run `Install-SonicSync.ps1` from PowerShell. The per-user installer places the application under `%LOCALAPPDATA%\Programs\SonicSync`, creates desktop and Start Menu shortcuts, and registers SonicSync in Installed apps. It does not require administrator privileges, install services/drivers, or change audio routing. Uninstall from Windows Settings > Apps > Installed apps or run `Uninstall-SonicSync.ps1`; it removes only installed program files and shortcuts, leaving any separate SonicSync data folders untouched. On normal close, the studio saves the WAV library, selected track, EQ profile, tone controls, and bypass setting in `%LOCALAPPDATA%\SonicSync\studio-session.txt`. Reopening restores the session with playback stopped. If Search is slow to list the new Start Menu entry, wait for Windows Search to refresh its index or launch the shortcut from Start Menu > SonicSync once.

## Security and privacy

The intended control service is LAN-only, paired explicitly with a short-lived one-time code, and authenticated with a random per-install secret. Production transport must use TLS and bind only to a private interface; never forward its port to the public internet. Audio stays local. No telemetry, recording, or cloud processing is part of this core.

## Third-party dependencies and licensing

The shared DSP currently has no third-party dependencies. No external driver, HRTF dataset, or proprietary SDK is redistributed. Later additions must record their license and redistribution terms here before packaging.

## Known limitations

* The Windows UI currently supports local WAV playlists only. GSMTC metadata and thumbnail decoding are implemented, but active Spotify, YouTube Music, and browser sessions have not been independently verified here. A player must expose its session and thumbnail through GSMTC; SonicSync does not scrape browser pages or fabricate missing covers. Release 1.0.0 was installed for the current Windows user and its launch, desktop shortcut, Start Menu shortcut, and Installed Apps registration were verified. Streaming-service playlist import, streaming playback, and measured HRTF rendering are unavailable on Windows.
* macOS and iOS/iPadOS audio paths have not been implemented or verified; this Windows host cannot run Xcode or validate Apple audio routing.
* The optional HTTPS remote host pairs with Android for Windows media transport. Device discovery and cross-device preset sync are unavailable. See [Windows remote setup](docs/windows-remote-control.md).
* The deterministic DSP suite checks that each built-in profile renders its catalog response (the 400-tone acceptance test stays within 0.25 dB; worst measured error 0.051 dB). Built-in profiles only cut, and gain is reduced automatically to leave headroom, so Hip-hop measures about -0.14 dB at 60 Hz and -0.43 dB at 100 Hz. Neutral measures 0 dB at 1 kHz and 8 kHz, and a +6 dB treble-shelf test measures +4.698 dB at 8 kHz. A separate process rendered a 100 Hz bypassed WAV through the Realtek endpoint while the loopback bridge captured and processed it to the LG endpoint: Neutral peak was 0.019989 in/out; Hip-hop measured captured peak 0.019989 and processed peak 0.0266779. This validates a live WASAPI loopback/DSP/render route fed by that test producer. It does not verify Spotify, YouTube Music, browser audio, virtual-cable routing, subjective sound quality, or precise latency.
* The WASAPI bridge supports endpoint mix formats with 1 to 16 channels. It does not compensate for clock drift between the source and output devices, so long sessions can accumulate delay or produce occasional dropouts.
* Crossfeed is a compact stereo headphone effect; it is not a measured HRTF renderer and does not recreate discrete 5.1/7.1 speakers.
* Track genre/identity is only available when the player or operating system exposes metadata. Audio analysis can suggest a broad profile but cannot reliably identify a song or genre.
* Android's local playback, server-provider code, queue, playlist import, Jamendo provider and permission-gated downloads are documented at [platforms/android/README.md](platforms/android/README.md). Actual Jamendo/Subsonic/Jellyfin server playback has not been verified. Mobile OS capability and device routing vary. An app-level processor affects audio rendered by that app, not arbitrary audio from other apps.

See [docs/architecture.md](docs/architecture.md) for the validated platform constraints and implementation sequence.
