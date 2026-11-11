# SonicSync architecture and platform capability matrix

## Decision

Use a platform-independent C++20 DSP library for consistent filters and presets, with native audio integrations per OS. Keep control/UI work off the audio thread and exchange parameters through a bounded handoff before any interactive controller is added. The first safe Windows listening path is app-owned WAV playback rendered to a user-selected endpoint, with a direct DSP bypass. WASAPI loopback remains a separate explicit relay and is never described as transparent system processing. A transparent system integration is a later installer-level feature, not something a normal desktop process can silently provide.

The core uses RBJ cookbook biquad designs (low/high shelf, peaking, high-pass, low-pass), coefficient interpolation on parameter changes, linked peak limiting, and optional stereo crossfeed. Generic HRTF processing is deferred until a distributable dataset with a clear license and an appropriate convolution implementation are selected.

## Platform capabilities

| OS | In-app processing | System-wide path | Control as a remote | Build verification here |
|---|---|---|---|---|
| Windows 11 | Native Win32 dark audio studio UI; app-owned mono/stereo WAV playback; live profile and simple EQ controls; 15 ms DSP bypass ramp; post-DSP 16-bin Goertzel spectrum; read-only GSMTC metadata and async thumbnail retrieval | APO or virtual endpoint integration; requires native packaging and may require signing/admin install | Not implemented | Release build and DSP tests pass; endpoint playback plus an in-session preset update verified; live media metadata/artwork not verified with an external player |
| macOS | Core Audio/Audio Units | Virtual audio device or supported Core Audio tap with permissions | Not implemented | No macOS/Xcode host |
| Android | Media3 local playback; optional Jamendo, HTTPS Subsonic/Navidrome, and Jellyfin providers; persistent queue/library, M3U/M3U8/PLS import, session-scoped Equalizer where exposed, adaptive bitrate ceiling | Arbitrary other-app audio processing is not available to ordinary apps; session-0/global insertion is deprecated | Not implemented | Android API 37: 12 JVM and 13 instrumentation tests pass; current APK installed/launched; no physical device or live Jamendo/Subsonic/Jellyfin server verified. |
| iOS/iPadOS | AVAudioEngine/Audio Units for app-owned audio | Arbitrary other-app audio processing is not permitted to ordinary apps | Not implemented | No compatible macOS/Xcode host |

## Windows trade-offs

WASAPI provides application access to endpoint streams and loopback capture from a render endpoint. The current bridge captures one selected render endpoint, processes mono/stereo audio through the shared DSP, and renders it to a different endpoint. Loopback capture does not replace the endpoint's playback route, so the original signal can remain audible and duplicate the processed output. An APO executes in the Windows audio engine and can participate in endpoint processing, but Windows documents it as an in-process COM object associated with audio driver installation. Real-time APO code has strict nonblocking constraints and deployment/compatibility obligations. SonicSync will not install or redistribute an unsigned driver as a shortcut.

## Mobile and Apple trade-offs

Android's `AudioEffect` APIs attach effects to audio sessions; the API documentation marks global output-mix session 0 insertion deprecated. Android standalone enhancement will therefore be limited to audio the app itself renders or to supported sessions. iOS AVAudioEngine supports app-owned signal graphs, while system audio access is scoped by Apple's APIs and permissions; an ordinary app cannot insert EQ into every other app's output.

## Protocol status

The JSON schema is a design artifact only; there is no Android pairing client or Windows control server in this build. A production implementation still needs a versioned envelope, short-lived pairing approval, authenticated TLS on a private LAN, bounded payload validation, replay protection, and actual DSP/control mapping. Do not open firewall ports or treat this schema as a running service. Audio samples must never be transmitted; only reduced visualization bins and metadata explicitly exposed by the platform are candidates for control updates.

## Implementation sequence

1. Shared DSP and deterministic tests — implemented and passing on MSVC x64; stability, preset precision, malformed values, bypass and peak protection are tested.
2. Windows app-owned WAV playback and basic desktop UI — implemented using event-driven shared-mode WASAPI; add files, select output/preset, play/stop/next and bypass are available. A named shared-memory control block carries preset and bypass state from UI to the player; the audio/render thread polls it at buffer boundaries, with no UI/network wait. A near-silent probe verified a live preset transition while all 288,000 input frames were submitted. DSP tests measure +4.404 dB at 60 Hz and +2.500 dB at 100 Hz for Hip-hop; a +6 dB treble shelf measures +4.698 dB at 8 kHz. GSMTC metadata is read on a background thread and displayed as metadata-only, but external-player metadata has not been tested here.
3. Windows loopback-to-render relay — implemented and built; it captures a selected render endpoint and processes it to a different output. It can duplicate source audio if the source is a physical device; it is not transparent interception. Device errors stop the route and the UI reports the failure.
4. Compact real-time spectrum visualization and current preset controls are implemented. A user-selected virtual-render-endpoint WASAPI loopback route now feeds the shared DSP and renders to another endpoint; live metering and shared controls are wired. A separate player-process probe verified Neutral 100 Hz peak 0.019989 in/out and Hip-hop processed peak 0.0266779 from the same 0.019989 captured peak. The host has no virtual cable, so a Spotify/browser route is not verified. Full parametric controls, richer preset management, and licensed HRTF/spatial processing remain future work.
5. Authenticated local controller service and desktop pairing.
6. Android and iOS controller clients; standalone in-app processors behind platform capability checks.
7. macOS audio path and platform-specific packaging.
8. Optional system integrations only after packaging, signing, and recovery paths are validated.
