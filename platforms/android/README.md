# Sonic Sync Android

The standalone Android application lives in `platforms/android`.

## Current implementation

- Kotlin and Jetpack Compose UI with the Eclipse Gold palette and the supplied brandboard icon.
- Local audio library discovery through Android `MediaStore` after the user grants audio access.
- Search by title, artist, and album.
- Local playback through Media3 ExoPlayer in a `MediaSessionService`, with playback controls, seeking, audio focus handling, and headphone-removal handling.
- A single playback-state snapshot derived from the Media3 session, saveable navigation destinations, and a mini-player that is hidden on the full Now Playing screen.
- Settings destinations for Sound & Audio, Playback, Library, Appearance, and About.
- An Equalizer entry point backed by Android's session-scoped `android.media.audiofx.Equalizer` where the active device exposes and grants control of that effect. The UI reads supported band frequencies/levels from the effect, persists settings, and reports when the effect is unavailable. This is device-specific and applies only to Sonic Sync playback; it is not a portable DSP implementation or system-wide EQ.
- A provider interface and real local MediaStore search adapter, with explicit unsupported results for unimplemented import/playback operations.
- Unified Discover searches on-device music, Internet Archive, configured Jamendo and music servers, and MusicBrainz concurrently. Results identify their provider and attribution, isolate provider failures, can be paged/cancelled, and expose Play/queue only when an accepted stream reference exists. Internet Archive streams are enabled only for items with a recognized CC/public-domain metadata license and a supported audio file; that uploader-supplied license is not independently verified, so check the item page. Archive downloads are disabled. MusicBrainz remains metadata-only.
- A configurable Jamendo catalog provider calls its official track API, plays the returned HTTPS stream through Media3, and accepts downloads only when Jamendo returns `audiodownload_allowed=true` plus a Jamendo HTTPS download URL.
- Configurable Navidrome/Subsonic and Jellyfin HTTPS providers with server authentication, search, library browsing, track metadata, and Media3 playback. Server credentials/tokens are encrypted with an Android Keystore AES-GCM key. Jellyfin sends its token in the `X-Emby-Token` HTTPS header; Subsonic's protocol-mandated salted token is used only in the active stream request. Remote stream URLs are excluded from the app's persisted queue and recent-play database.
- SQLite-backed local favorites, custom playlists, and recent-play history. Playlist membership stores MediaStore URIs and metadata only; local media is never copied. Duplicate playlist entries are ignored, and playlist items whose source files are missing are marked unavailable.
- Open **Queue** in Now Playing to play a specific entry or remove it. Removing a queue entry keeps its library track and file.
- Local entries in a mixed queue survive restarts in their original order. If a remote item was playing, restoration selects the next local item at its beginning, or the final local item if the stream was last. A remote-only queue clears the prior saved local queue. Position checkpoints update the playback state without rewriting unchanged track rows.
- A persisted Media3 queue with ordering, current entry, playback position checkpoints, shuffle, and repeat settings. On restart, the queue is restored paused; unavailable entries are skipped if playback reports a lost URI. Existing library data is retained by SQLite migrations.
- M3U/M3U8 and PLS playlist import through Android's document picker. The import manifest keeps unmatched entries and their source references; only unambiguous exact title/artist matches are linked to local playable files.
- Shuffle and repeat controls in Now Playing, with their selections persisted locally.
- JVM tests for search, duration formatting, mini-player visibility and destination restoration, and provider result/error behavior.
- Android instrumentation tests for SQLite favorites/playlist/history persistence and MusicBrainz metadata parsing.
- A Downloads screen backed by Android `DownloadManager`, with persisted status/progress, retry, cancel/remove, offline playback for completed files, and license links. Files are stored in app-owned external Music storage; no broad storage permission is requested.
- A persisted Media3 maximum audio-bitrate selection (no cap, 320, 192, or 96 kb/s) for sources that expose adaptive audio variants. Fixed-bitrate local/Jamendo/Subsonic/Jellyfin tracks generally provide one rendition, so the ceiling does not transcode or improve those files. Android's selected output route and actual decoded format remain device/source controlled.
- Remote HTTPS playback failures keep the track selected, show a safe reconnect/expiry message, and allow retry. Authentication, source, and URL details are not placed in that message.

## Playback verification (8 November 2026)

The current debug APK passed 23 JVM tests and 25 instrumentation tests on an x86 Android 8.0 emulator. The device run exercised the platform at the app's minimum supported API level. The fixture suite covers provider parsing, storage, playlists, downloads and remote-client validation.

A UI smoke test imported two 90-second WAVs through MediaStore, played and paused a track, switched to the second entry from the queue, and removed the first entry. After a force-stop and relaunch, the remaining track was selected and paused. Settings opened the Privacy & security screen. The Android pairing UI verified the certificate of a temporary loopback Windows host, completed locally approved pairing, read media-session state, and disconnected with revocation confirmed. The emulator ran headlessly with audio output disabled. Physical audio output and live account-backed music servers still require separate verification.

## Previous Android playback verification (2026-10-22)

- `testDebugUnitTest assembleDebug connectedDebugAndroidTest`: passed after this provider pass; 12 JVM tests and 13 API 37 emulator instrumentation tests, all passing.
- Debug APK: `app/build/outputs/apk/debug/app-debug.apk` (16,319,280 bytes, debug-signed).
- Installed and launched the final APK on a disposable API 37 x86_64 Google APIs emulator stored under `E:\SonicSyncToolchain\avd`; `MainActivity` was verified as the resumed activity. This is emulator, not physical-device, verification.
- Local audio playback was verified on the API 37 emulator. A later 30-second test signal was used for navigation checks; it naturally ended before the final status capture. Navigation tests confirm the queue metadata and mini-player visibility rules. Physical output is not verified.
- A final runtime check found and fixed a stale Recent screen: history had been persisted asynchronously by the playback service after the screen's earlier refresh. The app now refreshes after playback starts; an emulator playback session then appeared in Recent while still playing.
- Queue restoration was exercised with a local 30-second test file: after the app process was force-stopped and relaunched, Media3 restored the same item and queue paused at its most recent checkpoint. The periodic position checkpoint is five seconds, so abrupt process death can lose up to roughly one interval of seek position.
- The imported-playlist parser and database matching/persistence are covered by unit and instrumentation tests. The document-picker UI was compiled but a user-selected external playlist file has not yet been manually imported on a physical phone.
- Jamendo response parsing, HTTPS-host validation, and opt-in download authorization are covered by emulator tests. No production Jamendo client ID was available. The official documentation's test client ID returned `Jamendo Api Suspended Application Error`, so live catalog search, stream playback, and an actual download have not been verified.
- Android `DownloadManager` state persistence and rejection of metadata-only/untrusted URLs were tested. Completion, retry after a real network failure, low-storage behavior, and downloaded-file playback remain unverified against a real Jamendo download.
- Subsonic tests verify the documented salted-MD5 token example, response parsing, and HTTPS/embedded-credential URL rejection. Jellyfin tests verify item parsing, token-free stream URLs, host-scoped HTTPS authorization headers, and Android Keystore encryption at rest. No Navidrome/Subsonic or Jellyfin server credentials were available, so server login, library browsing, authentication renewal, and actual server streaming have not been end-to-end tested.
- Playback-quality limits update Media3's live `TrackSelectionParameters` and persist across activity reconnects. No configured adaptive-bitrate provider was available; actual rendition switching was not observed. The setting is a selection ceiling, not a codec, sample-rate, lossless, or Bluetooth-codec control.
- The emulator exposes a five-band Android Equalizer session effect. Sonic Sync read its band data, successfully changed a band level and read the value back, then restored neutral/disabled state. This verifies effect API control on this emulator, not an acoustic frequency-response change; no loopback measurement or physical listening test was performed.
- Shuffle and repeat operate through the active Media3 player; the selected modes are stored in local preferences and restored when the activity reconnects.
- Settings and Sound & Audio destinations were launched on the emulator. No physical Android device was attached.
- Android instrumentation tests passed for library database persistence across close/reopen and MusicBrainz JSON parsing. Unit tests passed for playback navigation and provider error classification.
- MusicBrainz was called from Discover on the emulator with a real query; the server returned HTTP 503. The app surfaced the failure and made no playback claim. Search-result rendering against a successful live response is not yet verified.
- Manually favorited an emulator test track, created a playlist, and added the track to it. The Favorites view and playlist count updated from SQLite. The DB persistence itself is also covered by the instrumentation test.
- During a running 30-second local test track, opening Now Playing removed the mini-player; returning to Library showed it again. Media3 reported the track still playing. Rotating the emulator to landscape preserved the Library destination and playback session; emulator rotation settings were restored afterward.
- No physical Android device was attached. No phone APK installation or hardware playback test was performed.

## Unified catalog milestone (2026-10-23)

- `testDebugUnitTest assembleDebug connectedDebugAndroidTest`: passed. 16 JVM tests and 21 API 37 emulator instrumentation tests passed with zero failures/skips.
- The updated debug APK was built at `app/build/outputs/apk/debug/app-debug.apk` (16,402,969 bytes; SHA-256 `84B375F63E02CA889BB448A01985E00F16AA3B9D59EB86E198D4BDBEEEF6AAFF`).
- Tests covered provider aggregation/failure isolation, result paging, title/artist matching, source-format preference, Archive license/playback gating, and persistence of online favorites/playlists without saving stream URLs.
- Instrumentation ran on the disposable API 37 emulator. The application was not manually exercised against live Internet Archive search or stream playback in this milestone; no physical-device verification is claimed.
- The build reports a non-fatal JDK 25 `sun.misc.Unsafe::arrayBaseOffset` deprecation warning from protobuf 3.25.5.
- The SDK Manager reports a duplicate `platforms/android-37.2-2` directory and skips it; the canonical SDK platform works for the build. The `avdmanager` device-profile command also reports that the system image lacks an optional `devices.xml`. A standard AVD configuration referencing the installed image was used; the emulator booted successfully. The SDK image itself was not modified.

## YouTube Music InnerTube catalog pass (2026-10-23)

- `testDebugUnitTest assembleDebug compileDebugAndroidTestKotlin`: passed. 16 JVM tests passed (0 failures/skips); app and instrumentation Kotlin sources compiled.
- `connectedDebugAndroidTest`: not run successfully; Gradle reported no connected devices. Direct ADB inspection failed because this execution environment has an empty `HOME` and ADB attempted to create `\\.android`, which is inaccessible. No host profile directory or permissions were changed.
- Debug APK: `app/build/outputs/apk/debug/app-debug.apk`, 16,423,030 bytes; SHA-256 `CF899D3C37F5CC732CFF06BBD3B8052DFED948C062339CFB61DBA4D675DD5A5`.
- New deterministic InnerTube request/parser/pagination/playlist tests compile as instrumentation tests, but were not executed without a connected emulator. A direct live endpoint probe was blocked by the execution environment's network (`HttpRequestException`). No live catalog request or playback is verified.
- YTM tracks remain non-playable in Media3. Public playlist metadata can be imported into Sonic Sync playlists, with exact local-file matches linked for playback; unmatched rows remain metadata-only.

## In-app YouTube player pass (2026-10-25)

- YouTube Music track rows now have a Watch action that opens YouTube's visible embedded video player in Sonic Sync. It preserves YouTube's controls and video; it does not resolve an audio-only stream or enter the Media3 queue/session.
- `CatalogPlaybackResolver` now validates existing local/HTTPS provider playback references before Media3 queue insertion and returns a specific unavailable result for YouTube Music. This does not resolve YouTube audio.
- `testDebugUnitTest` passed 21 JVM tests. `compileDebugAndroidTestKotlin` passed. Instrumentation tests and live YouTube playback were not run: ADB still cannot create its `\\.android` directory in this execution environment.
- `assembleDebug` succeeded with a one-off local debug keystore. The build reports the Android plugin metrics path warning (`C:\\.android`) and two deprecated window-color property warnings in the new Activity. No production signing config was added.
- Debug APK: `app/build/outputs/apk/debug/app-debug.apk`, 16,545,207 bytes; SHA-256 `313AE971B1ED053D6C8C5487F727E29B0973539D0CF1EC8A592F970DF35871A1`.
- Existing remote-stream Retry reopens the current URI. It cannot refresh an expired URL; searching/browsing the provider again is required for a fresh reference where available.
- The embedded player requires network access and has not been verified on an emulator or physical device. It may decline videos that require account/age verification. YouTube playback does not use Sonic Sync's Media3 equalizer, queue, or background controls.

Spotify, Apple Music, and direct YouTube Music Media3 playback/playlist-account synchronization are not implemented; current provider limits and required credentials are summarized in `../../docs/android-provider-feasibility.md`. YouTube Music has unauthenticated InnerTube catalog search and public-playlist metadata import, plus an in-app official embedded video player for tracks. The embedded player is not part of Sonic Sync's Media3 queue. See `../../docs/youtube-music-inner-tube.md`. Optional Windows pairing and remote GSMTC media controls are available when the separately launched Windows HTTPS host is running. This remote feature does not route or stream PC audio and does not synchronize libraries/playlists. It does not start a server or add firewall rules automatically. See `../../docs/windows-remote-control.md`. The app does not upload local files or send playback telemetry. Actual adaptive rendition selection, physical-device output, and device-specific audio processing remain unverified. No lossless or bit-perfect claim is made.

YouTube Music-specific architecture, Metrolist source review, InnerTube catalog integration, and the exact playback boundary are documented in `../../docs/youtube-music-inner-tube.md`. The original unified-catalog feasibility notes remain at `../../docs/unified-catalog-and-youtube-feasibility.md`.

The interface takes high-level usability cues from Metrolist's home/library/now-playing flows. It does not include Metrolist code or branding. The supplied Sonic Sync brandboard is stored in `../../assets/brand/sonicsync-3-eclipse-brandboard.png`; the app icon is an exact crop from its dark-mode icon panel.

## Toolchain

The verified host setup is on E::

- Android Studio `AI-262.9437.185.2621.16467767`, installed at `E:\andsdk`.
- Android Studio bundled OpenJDK `25.0.3` at `E:\andsdk\jbr`.
- Android SDK Platform 37.0 and 37.2, Build Tools 37.0.0, and Platform Tools 37.0.1 under `E:\SonicSyncToolchain\AndroidSdk`.
- CMake 3.31.6 and Android NDK 30.0.16248370 under the same SDK root. The NDK archive was downloaded from Google's official NDK download host and its published SHA-1 matched before extraction. Its `clang++` version command was verified. SDK Manager's NDK download failed; the verified archive was extracted manually, so Gradle/SDK Manager discovery of this NDK has not yet been verified.
- Gradle Wrapper 9.6.0 with the official distribution SHA-256 pinned in `gradle/wrapper/gradle-wrapper.properties`.
- Android Gradle Plugin 9.4.0, Compose BOM 2026.09.00, and Media3 1.11.1 are pinned in the Gradle build files.

SDK package and build dependency downloads require internet access on the first build. Build caches and SDK settings can be directed to E: with the environment below; no system-wide environment variables are required.

## Build and test (PowerShell)

From `platforms\android`:

```powershell
$env:JAVA_HOME = 'E:\andsdk\jbr'
$env:ANDROID_SDK_ROOT = 'E:\SonicSyncToolchain\AndroidSdk'
$env:ANDROID_HOME = $env:ANDROID_SDK_ROOT
$env:GRADLE_USER_HOME = 'E:\SonicSyncToolchain\GradleCache'
.\gradlew.bat clean testDebugUnitTest assembleDebug connectedDebugAndroidTest
```

`connectedDebugAndroidTest` requires a running emulator or USB-connected test device. JVM tests and APK assembly do not require one.

The debug APK is written to:

```text
app\build\outputs\apk\debug\app-debug.apk
```

## Install on a test phone

Enable Android Developer options and USB debugging, connect the phone, then run:

```powershell
$env:JAVA_HOME = 'E:\andsdk\jbr'
$env:ANDROID_SDK_ROOT = 'E:\SonicSyncToolchain\AndroidSdk'
$env:ANDROID_HOME = $env:ANDROID_SDK_ROOT
$env:HOME = 'E:\SonicSyncToolchain\AndroidUserHome'
$env:ANDROID_USER_HOME = $env:HOME
$env:ANDROID_SDK_HOME = $env:HOME
New-Item -ItemType Directory -Path $env:HOME -Force | Out-Null
& "$env:ANDROID_SDK_ROOT\platform-tools\adb.exe" devices -l
& "$env:ANDROID_SDK_ROOT\platform-tools\adb.exe" install -r '.\app\build\outputs\apk\debug\app-debug.apk'
```

Grant Sonic Sync's audio permission on first launch. The host had no physical phone attached during this milestone, so the commands above are ready for a phone but have not been verified against one.

## Dependencies and licenses

- Android Gradle Plugin and Android SDK/NDK: Google Android SDK license terms.
- Gradle: Apache License 2.0.
- Jetpack Compose, Activity Compose, and Media3: Apache License 2.0.
- Kotlin and kotlinx.coroutines: Apache License 2.0.
- JUnit 4: Eclipse Public License 1.0.
- AndroidX Test core, runner, and JUnit extension: Apache License 2.0.

MusicBrainz is used only for public recording metadata. Adding any additional provider requires a separate review of API access, playback/download permissions, attribution, authentication, and licensing.

## Android audio-effect limits

Sonic Sync requests Android's Equalizer audio effect for its own Media3 audio session. Android/device audio engines determine whether the effect exists, its bands, and whether the app has control. An ordinary app cannot rely on applying EQ to arbitrary other apps. The current C++ DSP core is still not connected to Media3 playback. A parameter readback does not prove acoustic EQ response; measure this on target hardware before claiming output characteristics.

## Provider status

MusicBrainz recording search is metadata-only and previously returned HTTP 503 from the emulator. Spotify Web API playlist import needs a registered OAuth PKCE client and user-granted playlist scopes; this app has no OAuth client. YouTube Data API can enumerate playlist metadata under quota and policy limits, but does not expose YouTube Music-specific access or grant audio delivery rights; this app has no Google API key/OAuth integration. Apple Music supports catalog/library and playlist APIs and documents MusicKit for Android, but requires a developer token, Music User Token, and user authorization; it is not integrated. See the provider feasibility document for official references and current boundaries.
