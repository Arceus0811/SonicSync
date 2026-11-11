# SonicSync audit baseline — 2026-10-21

## Architecture and build

The application is a native C++20 Win32 desktop UI. `shared/dsp` contains the portable biquad processor and preset codec. Windows integration is split into app-owned WAV playback, a separate WASAPI loopback-to-render relay, GSMTC/WIC metadata and artwork retrieval, and an interlocked shared-memory UI-to-engine control block. DSP processing is on each WASAPI event worker, not inside a COM audio callback. The external relay only receives apps explicitly routed to its selected source endpoint; it is not system-wide interception.

The active build is x64 Release with the Visual Studio 17 2022 generator, VS Build Tools 17.14.37806.7, MSVC toolset 14.44.35207, CMake 3.31.6, and Windows SDK 10.0.26100.0. CMake targets the Windows SDK installed on this host (the build log selected 10.0.26100.0). The shared DSP has no third-party dependencies; Windows APIs, GDI+, WIC, C++/WinRT, and the MSVC standard library are platform/toolchain dependencies. No Android, Flutter, Apple, APO, WDK, or virtual-device target exists in this CMake build.

Automated tests: `dsp_tests` measures frequency response and exercises filter stability, live preset transition, bypass, limiter, impulse/sweep/music-like signals, and preset serialization. `windows_artwork_tests` covers a valid bundled PNG and empty/invalid image data. The latest Release CTest run passed **2/2** in 0.12 seconds. These tests do not establish third-party media-session support, application compatibility, driver recovery, or subjective output quality.

## Baseline measurements

Measurements were taken on the current Windows host on 2026-10-21:

| Metric | Observed | Method and limit |
|---|---:|---|
| GUI executable | 313,344 bytes (306 KiB) | Release `sonicsync_gui.exe` |
| WAV player executable | 351,744 bytes (343.5 KiB) | Release `sonicsync_player.exe` |
| WASAPI relay executable | 320,000 bytes (312.5 KiB) | Release `sonicsync_wasapi.exe` |
| GUI launch-to-window | 81 ms | One process-launch sample until a window handle appeared; not a cold-start distribution |
| GUI idle CPU | 172 ms process CPU over 10 s (1.72% of one logical core) | One idle sample; no audio running |
| GUI memory | 31.5 MiB working set, 5.1 MiB private bytes | One idle sample; working set can change with cache state |
| GUI threads | 13 | One idle sample |
| DSP throughput, stereo 44.1/48/96/192 kHz | 0.31–0.33 / 0.34–0.35 / 0.68–0.71 / 1.36–1.41% of one core | Three runs of the in-process Release harness; synthetic two-tone block processing, 4 seconds of audio per buffer size, Hip-hop preset, limiter and crossfeed enabled |

The DSP benchmark is throughput-equivalent work, not a WASAPI callback deadline or whole-application playback CPU measurement. Across block sizes 128, 256, 480, and 960, observed throughput was about 71–320 times real time. The result leaves headroom on this host and does not justify SIMD, external DSP dependencies, or a more complex processing architecture at this point. The post-cleanup run remained in the same measured range; the cleanup removes duplicated spectrum code and is not claimed as a speedup.

To reproduce:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
1..3 | ForEach-Object { .\build\Release\dsp_benchmark.exe }
```

## Findings and next work

- `wasapi_player` decodes WAV data and allocates its bounded work buffer before starting the event loop. `wasapi_bridge` allocates its block and bounded sample queue before streaming. Disk reads happen before WASAPI streaming. Shared-memory controls are read at buffer boundaries. UI and GSMTC work stay off those audio worker loops.
- The player and relay previously duplicated the same 16-bin Goertzel/meter publication code. They now use `platforms/windows/audio_analysis.hpp`; the DSP signal path is otherwise unchanged.
- Preset serialization exists, but persistent user preset/settings storage and robust edit/undo management are not implemented.
- The relay is one shared profile for all apps routed through its source endpoint. It does not perform session-specific DSP, and its source may continue to play the unprocessed signal. The host had no virtual cable installed during the audit, so no third-party external app route was validated.
- The bridge supports mono/stereo endpoint mix formats only. Unplug/replug recovery, sample-rate changes during streaming, callback deadline distributions, and long-duration WASAPI soak performance have not been measured.
- Crossfeed is not HRTF binaural rendering. Pitch shifting, app-aware automatic profiles, secure pairing/control server, Android companion, and system startup option are not implemented.
- The 10-second GUI resource sample was idle. Playback resource use was not separately measured at the whole-process/device level; the DSP benchmark is a synthetic isolated throughput measurement.

Next engineering priority: install/use a virtual playback endpoint only with the user's explicit routing choice and validate external application audio and recovery. System-wide APO or custom endpoint work remains a separate signed/deployed Windows integration project; this repository does not currently include WDK, driver, or APO packaging.
