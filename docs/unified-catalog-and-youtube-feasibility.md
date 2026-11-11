# Sonic Sync unified catalog and YouTube Music feasibility

This document records the earlier official-API-only decision. The later unauthenticated InnerTube catalog implementation and current playback boundary are documented in [youtube-music-inner-tube.md](youtube-music-inner-tube.md); statements below that the provider is not enabled are historical and superseded.

Checked 2026-10-23. This records the inspected upstream projects and the implemented Android behavior; it is not legal advice.

## Metrolist investigation

Metrolist is a separate Kotlin/Jetpack Compose Android application. Its repository identifies a GPL-3.0 license, a dedicated `innertube` module, and a dependency on `com.github.MetrolistGroup.innertubex`; the module also uses Ktor for HTTP. Its project README describes search, track playback, caching/downloads, account login, and library synchronization. The login source reads a YouTube Music WebView session cookie and related visitor/account fields. The InnerTubeX project describes private InnerTube search/player operations, stream extraction, format selection, cipher handling, and authenticated cookies/session data. This is an unofficial client path that depends on undocumented, changeable service protocols and can break when YouTube changes them.

Metrolist's app code is GPL-3.0. Incorporating or adapting covered code into a distributed Sonic Sync application would require a separate GPL compatibility and source-distribution review. Its license covers the software code, not the music, stream access, artwork, or redistribution rights. Sonic Sync does not copy Metrolist code or use InnerTubeX/NewPipe extraction. This avoids adding a large, policy-sensitive dependency and avoids collecting Google session cookies.

## YouTube Music decision

The official YouTube Data API provides search, video, playlist, and playlist-item metadata. It does not expose a YouTube Music audio stream endpoint. Official policy prohibits separating audio from video, offline copies outside supported flows, background playback via an API client, and altering YouTube playback. An embedded YouTube player must retain its own branding and controls and cannot be replaced with an audio-only Media3 source. A metadata search could be considered later with a Google Cloud project/API key or OAuth, quota handling, YouTube attribution, and a compliant player/link flow; it would not be an independent music stream in Sonic Sync's player.

At the time of this note, no YouTube Music or YouTube provider was enabled. Sonic Sync has since added unauthenticated InnerTube search and public-playlist metadata browsing without stream extraction. Users can open the official service separately; its audio is not added to Sonic Sync's queue or DSP path.

## Implemented discovery behavior

Discover now searches enabled providers concurrently and reports failures independently. Providers without credentials are skipped in “All sources”; configured server providers are included. Results retain a source name, attribution, and track-specific playability. The results list has paging, cancellation, source links, and a queue action. Play and queue are exposed only when the catalog object includes an accepted playable reference. The current page slicing is local paging over provider result batches, not a claim of deep server-side pagination.

Providers in the aggregate search:

- On-device MediaStore tracks (requires Android audio permission).
- Internet Archive audio items, with playable streams only when its metadata declares a recognized Creative Commons/public-domain mark and a supported audio file exists. A user-declared Archive license is not independently verified; open the item page and check rights before use. Download is not enabled for this source.
- Jamendo, if a user-configured developer client ID is present. The per-track API flags still gate downloads; live credentials are not available in this environment.
- Configured Navidrome/Subsonic and authenticated Jellyfin servers.
- MusicBrainz metadata-only results. MusicBrainz results stay non-playable.

Free Music Archive is not integrated: its official app-developer page says its API was shut down, prohibits hotlink playback from its servers, and prohibits forwarding user queries to scrape its HTML without approval. A later approved hosting/API agreement would be needed before adding it.

## Stream quality

For Archive.org items, when metadata exposes multiple supported audio files, Sonic Sync prefers FLAC on Android API 27+, then WAV, Ogg/Vorbis/Opus, AAC/M4A, then MP3, and breaks a same-format tie by file size. FLAC is excluded below API 27 because ExoPlayer needs a platform FLAC decoder (required at API 27) or an additional decoder extension, which this app does not bundle. The app labels the Archive-reported source format. It does not infer measured bitrate/sample rate from the file size, transcode files, or claim bit-perfect output. Jamendo and server providers use the stream their APIs/servers return; the Media3 bitrate ceiling only affects providers that expose alternate adaptive renditions.

## Verification and external requirements

The final 2026-10-23 Android run passed `testDebugUnitTest assembleDebug connectedDebugAndroidTest`: 16 JVM tests and 21 API 37 emulator instrumentation tests, zero failures/skips. Coverage includes concurrent aggregation with provider failure isolation, result paging, title/artist matching, format selection/API-level fallback, Archive license/audio-stream gating, and persistence of catalog favorites/playlist metadata without persisting stream URLs. The debug APK is `platforms/android/app/build/outputs/apk/debug/app-debug.apk` (16,402,969 bytes; SHA-256 `84B375F63E02CA889BB448A01985E00F16AA3B9D59EB86E198D4BDBEEEF6AAFF`).

The tests do not verify an Internet Archive live query or playback; that still requires a real network/device run. The emulator tests exercise deterministic response fixtures, not the live Archive service. Jamendo still requires an active client ID. Navidrome and Jellyfin still require configured servers. YouTube Music playback is intentionally unsupported through Sonic Sync's player under this architecture.

## Sources

- [Metrolist repository and feature/maintenance notes](https://github.com/MetrolistGroup/Metrolist)
- [Metrolist InnerTube module dependencies](https://raw.githubusercontent.com/MetrolistGroup/Metrolist/main/innertube/build.gradle.kts)
- [Metrolist dependency versions](https://raw.githubusercontent.com/MetrolistGroup/Metrolist/main/gradle/libs.versions.toml)
- [Metrolist login source](https://github.com/MetrolistGroup/Metrolist/blob/main/app/src/main/kotlin/com/metrolist/music/ui/screens/LoginScreen.kt)
- [Metrolist GPL-3.0 license](https://github.com/MetrolistGroup/Metrolist/blob/main/LICENSE)
- [InnerTubeX (unofficial API/extraction project, GPL-3.0)](https://github.com/MetrolistGroup/innertubex)
- [YouTube Data API reference](https://developers.google.com/youtube/data/reference/rest)
- [YouTube developer policies](https://developers.google.com/youtube/terms/developer-policies)
- [YouTube developer policy compliance guide](https://developers.google.com/youtube/terms/developer-policies-guide)
- [Internet Archive Advanced Search API](https://doc-tools.readthedocs.io/en/latest/ia-test-gsod/item-search-apis.html)
- [Internet Archive metadata API](https://doc-tools.readthedocs.io/en/latest/ia-test-gsod/md-read.html)
- [Android Media3 supported formats](https://developer.android.com/media/media3/exoplayer/supported-formats)
- [Free Music Archive app-developer rules](https://freemusicarchive.org/app-developers)
