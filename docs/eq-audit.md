# Equalizer audit, 9 November 2026

Baseline is the source and packaged Windows 1.0.2 studio before EQ changes. Line numbers refer to that source.

## Signal and UI paths

- In `platforms/windows/gui.cpp:492` a preset click updates the selected index and publishes it. `drawEq` at line 322 highlights that index. The three sliders draw independent offsets, which line 422 edits without changing the preset identity. Selecting another preset retains those offsets. The UI can therefore show Rock while its sliders describe an earlier edit.
- Lines 116–117 publish separate Interlocked fields to **both** open mappings. The player reads them at each writable buffer in `wasapi_player.cpp`. The external relay also reads them at capture-packet boundaries in `wasapi_bridge.cpp:295`. Both reconstruct the preset locally and apply the three offsets. The relay does receive live updates. Neither reader gets a coherent multi-field transaction. The GUI creates a GUID-named local mapping during launch with the process default DACL. Stopping the child releases the mapped view and handles.
- The graph at `gui.cpp:320` calls `responseDb` on a separately assembled preset at a fixed 48 kHz. `shared/dsp/dsp.cpp:66` and line 237 use the same RBJ design routine. The graph uses cubic drawing between samples, reserves no left gutter for its labels, and keeps displaying the wet curve while bypassed.
- `shared/dsp/dsp.cpp:92` rejects more than two channels. RBJ state is already double-precision transposed direct form II. The renderer uses fixed banks, numeric-only preset updates and a 50 ms crossfade. A pending update can wait for the previous fade plus a second fade. Invalid low-rate bands become default unity filters. The response evaluator can instead clamp and evaluate those bands. No explicit denormal suppression exists. Neutral still passes through the limiter. The player decodes and requantizes PCM even in settled bypass, changing integer samples.
- `wave_reader.hpp:90` and `wasapi_bridge.cpp:222` reject multichannel input. The relay assumes a fixed requested buffer duration. The Win32 Devices page manually refreshes endpoint enumeration.
- Android's `PlaybackService.kt:79` builds the stock ExoPlayer. Lines 134 and 173–181 route custom EQ commands to a platform audio effect tied to the generated session ID. `SessionEqualizer.kt:44` restores levels by device band index. There is no shared processor, ten-preset selector or route-aware preset mapping. Hardware failure can leave EQ unavailable. The existing UI explicitly reports that condition.

## Visual reproduction

The running packaged studio showed the truncated E in the Active Profile card and a transport row partly under the taskbar. The Edit button at line 311 leaves just 20 pixels for its text after the margins in line 111. The initial 1240 by 820 logical window at line 562 exceeds the scaled desktop work area. The transport row itself is anchored inside the oversized client. The EQ sliders show near-zero offsets even with Rock selected. The 0 dB text occupies the plot at its zero line. The captured desktop used its actual scaling, not a forced 150% display configuration.

[Home capture](eq-evidence/before-home.png) · [Equalizer capture](eq-evidence/before-eq.png)

## Baseline response comparison

Computed from the original RBJ definitions at 48 kHz over 2,049 log-spaced points from 20 Hz to 20 kHz, with no limiter or crossfeed. Each preset was compared to every other preset. These are calculated transfer functions, not loopback recordings.

| Preset | Minimum dB | Maximum dB | Closest preset | Maximum difference (dB) |
|---|---:|---:|---|---:|
| neutral | -0.00 | 0.00 | classical | 1.49 |
| music | 0.00 | 1.50 | neutral | 1.50 |
| hip-hop | 0.00 | 4.99 | rnb | 1.50 |
| rnb | 0.08 | 3.49 | hip-hop | 1.50 |
| classical | -0.98 | 1.49 | acoustic | 1.00 |
| acoustic | -0.97 | 1.98 | classical | 1.00 |
| rock | -0.84 | 2.50 | gaming | 1.50 |
| movies | 0.02 | 2.50 | gaming | 1.50 |
| gaming | 0.02 | 1.51 | acoustic | 1.00 |
| voice | -26.14 | 1.91 | classical | 26.14 |

## Implementation and verification plan

1. Generate the catalog from one checked-in JSON file. Define control state, deterministic response and headroom calculation. Keep compatibility with legacy session and preset readers.
2. Transfer one bounded coherent snapshot to the player and relay. Measure the processor, rapid changes, low rates and multiple channels. Build and test before changing layout.
3. Repair sizing and scrolling, expose editable preset values, and verify captures of the packaged UI.
4. Connect the same DSP to Android PCM playback and exercise supported fallback, route and device paths. Report endpoint, device and timing limits separately from measured results.
