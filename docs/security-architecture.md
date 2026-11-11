# Security architecture (Android + optional Windows remote host)

Review date: 25 October 2026. This describes implemented boundaries, not a claim of formal certification.

## Android

- Local library/history/queue/download metadata uses app-private SQLite. SQL statements in `PersonalLibraryDatabase` use selection arguments for external values. Audio access is limited to Android's media permission.
- Provider server settings are serialized to JSON and encrypted with AES/GCM using an Android Keystore AES key. The Jamendo client ID and generated Jellyfin device ID are ordinary private preferences; neither is an account secret.
- Android backup is disabled in the manifest and explicit backup/data-extraction rules exclude the app's files, database, preferences, and app-owned external data.
- `MainActivity` is exported only as the launcher. `YouTubeEmbedActivity` is not exported. `PlaybackService` is exported for the Media3 media-session contract; custom EQ/provider commands are only advertised and accepted for this app's package. Standard media-session controls remain available to Android media controllers.
- The YouTube WebView needs JavaScript to run YouTube's visible player. File/content access, file-origin JS access, and mixed content are disabled; no JavaScript bridge is installed; top-level navigation is restricted to YouTube embed URLs; Safe Browsing is enabled.
- Server APIs require HTTPS, reject embedded URL userinfo, disable automatic redirects, use bounded timeouts and response bodies, and send Jellyfin tokens in a header. Subsonic's implemented protocol uses its token/salt query parameters, so the configured server can see them in request URLs. Never enable plaintext HTTP for those credentials.
- Windows remote Android client disables redirects, bounds response bodies, restricts commands, and pins the exact certificate fingerprint confirmed by the user. A certificate pin is trust-on-first-manual-confirmation, not a public CA identity check.

## Windows remote host

The helper is launched manually with an explicit assigned IP and port. It uses HTTPS with a self-signed certificate, TLS 1.2 minimum, and DPAPI-protected PFX password. It creates no firewall rule and is not started by the GUI. Pairing uses a random six-digit code, five-minute expiry, five-failure lockout, a console approval, and a random 256-bit in-memory bearer that expires after 12 hours. The route table is fixed to health, pair, state, control, and disconnect; body size is capped at 16 KiB and media command arguments are fixed. Authenticated mutating requests reject repeated request IDs (bounded recent-ID set per session). The spawned native process receives only a fixed executable and fixed action argument.

Default port is 47931 but the operator can choose any port 1024–65535. The listener binds only the explicitly provided local IP. The product does not configure Windows Firewall or router forwarding. Stop the host to revoke all tokens. No persistent Windows startup service is created.

## YouTube implementation boundary

`YouTubeMusicProvider` makes unauthenticated catalog search and playlist requests to an unofficial web endpoint. It does not call player endpoints, extract direct URLs, decode signature ciphers, obtain PO tokens, collect Google credentials/cookies, or add stream URLs to Media3. YouTube results use the visible embedded player and are not background Media3 audio. This is both a capability boundary and a product/legal choice; signature handling is not necessarily DRM decryption, but an extractor that recreates undocumented delivery and token workflows is not added to this product.

## Storage and backups

Backups are disabled to avoid exporting encrypted credentials, device-bound Keystore data, listening history and queue/library state. This trades cloud/device migration for reduced credential exposure. Users should export playlists through supported app functions if available and understand exports can contain media references.

## Known limitations

- The Windows host's bearer requests do not persist paired devices; tokens are intentionally volatile and require re-pairing after host restart. Sessions now expire after 12 hours.
- Mutating authenticated messages use request IDs with a bounded per-token replay cache; read-only state polling has no request body and is protected by TLS plus the bearer token.
- Self-signed certificate pin confirmation depends on the user comparing the PC fingerprint through a trusted channel.
- Subsonic auth query parameters can appear in server/proxy access logs. The configured server is trusted by the user; prefer a private server with protected logs.
- No automated dependency vulnerability scanner or third-party penetration test was run in this change.
