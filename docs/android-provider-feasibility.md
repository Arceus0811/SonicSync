# Android music provider feasibility

Checked 2026-10-22 against the providers' official developer documentation. This is an engineering feasibility note, not legal advice or evidence of an approved commercial integration.

## Current application boundary

The Android app plays user-selected local MediaStore items, has metadata-only MusicBrainz search, and now includes an optional Jamendo catalog adapter. Jamendo playback is enabled only for HTTPS stream URLs returned by its API; downloads additionally require the per-track `audiodownload_allowed` flag and a Jamendo HTTPS download URL. A catalog result or provider reference does not imply permission to stream, cache, or download an item. The documented test client ID returned `Jamendo Api Suspended Application Error`, so live Jamendo search, streaming, and downloads remain unverified. MusicBrainz currently returns HTTP 503 from the emulator.

## Provider options

| Provider | Feasible role | Access and constraints | Decision |
| --- | --- | --- | --- |
| Spotify | OAuth-backed playlist and catalog metadata import; link back to Spotify for playback | OAuth scopes are user-granted; private and collaborative playlist access have separate scopes. Web API access does not grant delivery rights. Spotify prohibits downloading/stream ripping; use of Spotify content to alter or mix audio is restricted by platform rules. API rate limits can return 429. | Not implemented. Requires a registered public client, Authorization Code with PKCE, user consent, policy review, and secure token revocation/storage. Do not stream Spotify audio in Sonic Sync or apply Sonic Sync DSP to Spotify content. |
| YouTube / YouTube Music | YouTube Data API playlist metadata import and links to official playback | `playlistItems.list` is quota-metered (1 unit per call in current docs) and requires authorized API access for private user content. YouTube policy prohibits separating audio/video, downloads outside supported flows, modifying media portions, and background playback through API-derived content. | Not implemented. YouTube Data API could support metadata-only imports with a Google Cloud project, API key/OAuth, quota handling, and a local matching UI. It does not establish access to YouTube Music playlists or grant Sonic Sync audio playback. |
| Apple Music | Catalog and user-library metadata; in-app playback may use Apple's Android MusicKit SDK | Apple Music API requests need a developer token. Personalized library access needs a Music User Token. The developer token requires a Media Services identifier/private key and is rate-limited (429 on excess). | Not implemented. Requires an Apple developer team, private signing key, Music User Token consent, subscriber eligibility where applicable, and Android MusicKit SDK integration. Keep playback in the authorized MusicKit path rather than copying restricted audio into local storage. |
| Jamendo | Implemented configurable catalog search and Media3 streaming for returned HTTPS URLs; optional downloads are gated per item | Calls require a developer `client_id`; private user data uses OAuth. Jamendo requires acceptance of API terms. Download availability is item-specific (`audiodownload_allowed`); each item's license and intended use must be honored. Read/write access has approval requirements, and high volume can trigger review. | Code and authorization checks are tested, but live use needs a valid user-supplied client ID and review of Jamendo terms and per-track licenses. No claim of blanket free-use rights. |
| Navidrome / Subsonic-compatible servers | Implemented HTTPS search, limited album/library browsing, and authorized Media3 track streams | Requires a user-owned compatible server, trusted TLS certificate, account username/password, and Subsonic token authentication. | Code and response tests pass; no server credentials or live Navidrome instance were available for end-to-end testing. Password is encrypted with Android Keystore at rest. |
| Jellyfin | Implemented HTTPS sign-in, audio search/library browsing, and Media3 stream playback | Requires a reachable Jellyfin server, valid user account, trusted TLS certificate, and server-supported API. The access token is encrypted at rest and sent in the HTTPS `X-Emby-Token` header. | Code and parser/secure-storage tests pass; no Jellyfin server or credentials were available to test server-side playback. |
| MusicBrainz | Recording metadata search and local-library enrichment | It is metadata, not audio hosting. Requests must stay at or below one request/second, use a meaningful User-Agent, and commercial use requires checking its commercial plans. | Implemented as metadata-only search with a 10-minute, 30-entry in-process cache and serialized requests. Emulator network request currently gets HTTP 503, so live results remain unverified. Add an actual maintainer contact to the User-Agent before distributing broadly. |

## Credentials, rights, and implementation prerequisites

- Spotify: register an application, implement OAuth Authorization Code with PKCE for a native app, request only needed scopes, store tokens securely, and satisfy current Spotify platform/design/policy requirements. Playlist import should preserve Spotify IDs/links and attribution; playback remains in an authorized Spotify client.
- YouTube: register a Google Cloud project and enable YouTube Data API v3; use OAuth for the signed-in user's private data; account for quota and API policy audits. Open links in the official YouTube/YouTube Music app or compliant player. Never scrape pages or extract media URLs.
- Apple Music: register a Media Services identifier and private key, generate developer tokens in a protected signing environment, obtain user authorization for library access, and account for subscriber entitlement and token rate limits. Review current MusicKit Android SDK licensing and App Store/Play distribution implications before implementation.
- Jamendo: create a developer account/application, use its `client_id`, accept the API terms, and check the license on every track. Obtain approval for APIs/plans that require it. A catalog stream URL or `content_id_free` flag does not itself establish rights for every use or download.
- Navidrome/Subsonic: expose a trusted HTTPS URL to the phone (do not open router ports as part of app setup), create a least-privilege music account, and configure the server URL/user/password in Sonic Sync. Server URLs containing user-info, query credentials, or plain HTTP are rejected.
- Jellyfin: configure a trusted HTTPS reverse proxy or server certificate, create a user limited to the intended library, then sign in from Sonic Sync. The app stores the returned token encrypted and does not put it in the stream URL or persisted playback queue.
- MusicBrainz: use a descriptive User-Agent, no more than one request per second, cache results, and review commercial use terms before monetized distribution.
- For every provider, review artwork attribution, caching/retention, privacy disclosures, OAuth redirect handling, token revocation, quotas, and provider branding requirements before release.

## Recommended order

1. Keep MediaStore as the default playable provider and persist user playlist metadata locally.
2. Add import/export through documented playlist formats (for example M3U) with explicit URI matching to local files.
3. Add a metadata-only Spotify and YouTube import adapter only after credentials and current terms are reviewed; store provider IDs and deep links, and report unmatched local tracks honestly.
4. Navidrome/Subsonic and Jellyfin providers are implemented; validate against a real, trusted HTTPS server before release. Jamendo adapter is implemented but requires an active client ID and per-track license review.
5. Add MusicBrainz only as a rate-limited metadata resolver if local tag matching proves useful.

## Official references

- [Spotify playlist access and scopes](https://developer.spotify.com/documentation/web-api/concepts/playlists)
- [Spotify OAuth authorization](https://developer.spotify.com/documentation/web-api/concepts/authorization)
- [Spotify Get Playlist Items](https://developer.spotify.com/documentation/web-api/reference/get-playlists-items)
- [YouTube Data API playlistItems.list](https://developers.google.com/youtube/data/reference/rest/v3/playlistItems/list)
- [YouTube developer policy guide](https://developers.google.com/youtube/terms/developer-policies-guide)
- [Apple MusicKit for Android](https://developer.apple.com/musickit/)
- [Apple developer token requirements and rate limiting](https://developer.apple.com/documentation/AppleMusicAPI/generating-developer-tokens)
- [Jamendo API overview and terms](https://developer.jamendo.com/v3.0/docs)
- [Jamendo OAuth and application access](https://developer.jamendo.com/v3.0/authentication)
- [Jamendo track stream/download permissions](https://developer.jamendo.com/v3.0/tracks)
- [Subsonic API: authentication, search, album browsing, and stream endpoints](https://subsonic.org/pages/api.jsp)
- [Jellyfin server API reference](https://api.jellyfin.org/)
- [Jellyfin Kotlin SDK authentication guide](https://kotlin-sdk.jellyfin.org/guide/authentication.html)
- [Spotify playlist API concepts and scopes](https://developer.spotify.com/documentation/web-api/concepts/playlists)
- [YouTube Data API playlistItems.list](https://developers.google.com/youtube/v3/docs/playlistItems/list)
- [Apple Music API and playlist endpoints](https://developer.apple.com/documentation/applemusicapi/)
- [MusicBrainz API policy and rate limits](https://musicbrainz.org/doc/MusicBrainz_API)
