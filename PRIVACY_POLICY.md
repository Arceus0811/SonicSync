# Sonic Sync Privacy Policy

**Version:** 1.0  
**Last updated:** 25 October 2026  
**Status:** Project policy text; it has not been published at a public URL and is not a substitute for jurisdiction-specific legal review.

## Scope and privacy defaults

This policy describes the current Sonic Sync Android 3.0 alpha and the optional Windows remote-media helper in this repository. Sonic Sync does not include advertising analytics or an automatic crash-reporting SDK. The app does not silently upload a local library, favorites, playlists, listening history, or diagnostics. Using a network provider does send the request needed for that feature to the selected provider; those services have their own retention and privacy rules.

## Android permissions and local data

The manifest and merged Media3 dependencies declare Internet access, audio-library access (`READ_MEDIA_AUDIO` on current Android and `READ_EXTERNAL_STORAGE` through Android 12), `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_MEDIA_PLAYBACK`, `POST_NOTIFICATIONS`, `ACCESS_NETWORK_STATE`, and `WAKE_LOCK`. The last two support network-state detection and background media playback. Audio access is used to list/play device audio. The manifest does not request contacts, location, microphone, camera, SMS, call logs, accessibility, device-admin, or VPN access. Android may control when a runtime permission is granted.

The app-private SQLite database stores local media metadata, favorites, playlist metadata and entries, imported playlist references, playback history, queue/position, and download records. Actual audio files are not copied into that database. Authorized downloads are placed in Sonic Sync's app-owned music folder using Android DownloadManager. App cache may contain temporary files. Settings/preferences include playback settings, a public Jamendo client identifier, and a non-secret Jellyfin device identifier.

Subsonic/Navidrome server configuration, Jellyfin access tokens, and the Windows pairing credential are encrypted using AES-GCM with an Android Keystore key. The key is non-exportable through this app. Encryption does not protect data while the device/app is unlocked or against a compromised OS. App backup and Android data extraction are disabled because this data is device-bound; this also means the library, settings, and credentials are not automatically restored to a new device.

Search history is not persisted by the current app. Playback history is stored locally and can be cleared in Privacy & Security. Cache can be cleared there. Downloads can be reviewed and removed separately. Clearing a provider removes its saved configuration from this device; it cannot delete requests or account records already held by a provider.

## Network requests and providers

Network features are user-initiated and use HTTPS where the provider supports the implemented integration:

| Feature | Data sent | Destination / limitation |
|---|---|---|
| YouTube Music catalog | Search text, public catalog request context, playlist ID for import | Unofficial unauthenticated InnerTube web endpoint at `music.youtube.com`; this is not an official API integration. Catalog metadata is displayed and the video can be opened in a visible YouTube embedded player. Sonic Sync does not resolve YouTube audio for Media3. |
| MusicBrainz | Search terms / metadata lookup | MusicBrainz public service. |
| Internet Archive | Search terms and selected item requests | Internet Archive public service; available source URLs are passed to Media3 when present. |
| Jamendo | Search terms and configured public client ID | Jamendo API; its public client identifier is not a password. Download requests are made only for items marked downloadable by provider metadata. |
| Navidrome / Subsonic | Server URL, username, token-authenticated API query and selected media ID | User-configured server over HTTPS. The Subsonic protocol implemented here uses token and salt query parameters; these are sent to that server in request URLs while browsing/streaming. The server operator may log request URLs. |
| Jellyfin | Server URL, account name/password during sign-in, then access token and media requests | User-configured server over HTTPS. The password is exchanged for a token and not retained by the sign-in flow; the token is encrypted locally and sent to the configured server for authorized calls. |
| Windows remote media | Explicit host, port, pairing code, device name, certificate pin, bearer credential, and requested media-session operation | User-started Windows helper on the chosen local address. TLS is certificate-pinned by Android. Only media-session state and play/pause/next/previous controls are supported. |

Provider requests are not tunneled through Sonic Sync servers. Track metadata and provider artwork may be fetched from their provider/CDN URLs. Sonic Sync does not collect account passwords for Google and does not read browser cookies or private Google session data. No search history is retained by Sonic Sync, but providers can process or log requests under their policies.

## Windows desktop library

On normal close, the desktop studio stores the WAV paths you added, their order, selected track, EQ profile, tone controls and bypass setting in the current user's local SonicSync folder. Audio files stay in their original locations. To clear these preferences and paths, close the studio and delete `%LOCALAPPDATA%\SonicSync\studio-session.txt`.

## Windows companion and pairing

The Windows network helper is a separate, opt-in foreground process. It is not started by the Windows GUI or Android app. The user explicitly starts it, chooses an IP address and TCP port (default 47931), and must locally approve each phone by typing `yes`. It binds only to that specified local address; Sonic Sync does not create firewall rules, router forwarding, UPnP mappings, or public tunnels. Keep it on a trusted private network and stop it when not needed. The self-signed TLS certificate and DPAPI-protected PFX password are stored under the current Windows user's LocalAppData SonicSync directory. Pairing tokens are random and remain in host memory until disconnect or host shutdown. The phone stores its token in Android Keystore-encrypted preferences.

The helper returns active Windows media-session title, artist, album, source app and playback state where Windows exposes them. It accepts only a fixed list of media transport operations; it cannot accept shell commands or modify audio routing. The current helper uses TLS 1.2 or later, a short-lived six-digit pairing code, local approval, a global lock after repeated incorrect codes, and a 12-hour bearer-token expiry. Tokens are volatile and are cleared on disconnect or host stop. It has no automatic listener, firewall rule, or Windows Settings interface.

## Logs, crash reports, and third parties

The Android source has no third-party analytics or crash-reporting dependency. App/provider errors are shown locally. As with any Android process, operating-system diagnostics may exist outside Sonic Sync's control. Do not send support logs or screenshots containing provider URLs, media details, or device addresses unless you choose to share them. Sonic Sync does not currently create a diagnostic-export feature.

Third-party providers may retain search, authentication, playback, IP-address, and request metadata according to their own terms. Deleting app data or disconnecting a provider does not erase data already received by those providers.

## User controls and policy updates

Use **Settings → Privacy & Security** to clear local playback history/cache, open Android permission settings, manage downloads, remove provider configuration, and disconnect the paired Windows phone credential. If the phone is offline during Windows disconnect, stop the Windows helper to revoke all in-memory tokens. Android system Settings can revoke media/notification permissions. Uninstalling or clearing app storage removes app-private data; app-owned download behavior is governed by Android storage rules.

Before a release or provider/permission/storage/network change, the maintainers must inspect the implementation, update this policy and the in-app disclosure together, update `docs/security-architecture.md` and `docs/security-audit.md`, and record the review date. A public distribution should publish this file at a stable HTTPS URL and replace the in-app repository-file reference with that URL. No public URL is claimed here.

## External policy links

- YouTube Terms of Service: <https://www.youtube.com/t/terms>
- Google Privacy Policy: <https://policies.google.com/privacy>
- Provider terms/privacy pages should be reviewed directly when configuring those services.
