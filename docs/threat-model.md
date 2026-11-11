# Sonic Sync threat model

**Version 1 · reviewed 25 October 2026**

## Assets

Provider credentials/tokens; Windows pairing token; library metadata, playlists, favorites, playback history and queue; downloaded files; provider search terms; Windows media-session metadata and remote transport controls.

## Trust boundaries and threats

| Boundary | Threat | Existing mitigation | Remaining risk |
|---|---|---|---|
| Android app ↔ OS/media library | Excess permissions or hostile media metadata | Narrow audio/notification/media-playback permissions; app-private DB; provider text is rendered as text | Android OS, granted content URIs, and malformed metadata remain dependencies |
| Android app ↔ provider | Network interception, oversized/invalid replies, credential leakage | HTTPS, bounded timeout/body, no redirects for server APIs, input checks; Keystore encryption | Providers see requests; Subsonic requires token/salt in URLs; unofficial YouTube API may change |
| Android local storage/backups | Backup or casual filesystem extraction | Private app storage, AES-GCM server config, Keystore key, backups disabled | Unlocked/rooted/compromised device can access app runtime data; settings and public client IDs are not all encrypted |
| Android WebView ↔ YouTube content | File access, mixed content, JS bridge abuse, navigation abuse | Visible YouTube player only; JS required; file/content access and mixed content disabled; no bridge; non-exported activity | Third-party YouTube page code runs in WebView and receives its normal network context |
| Android ↔ Windows LAN | Unauthorized control, MITM, internet exposure | Manual host start and IP bind; TLS pinning; one-time expiring code, local approval, random token, fixed routes/commands; no automatic firewall/UPnP | Trusted LAN and correct manual fingerprint comparison required; control request replay cache is absent |
| Windows helper ↔ OS | Arbitrary process invocation or persistence | Fixed executable/action mapping; foreground manually launched process; no service/task/startup; DPAPI-protected certificate password | Windows user account and selected listener interface are trusted; host process is not sandboxed |
| Downloads/files | Path traversal/overwrite | Android DownloadManager app-owned destination; sanitized names and provider allowlist in download manager | Provider server/content and Android DownloadManager remain external dependencies |

## Adversary assumptions

We defend against unauthenticated LAN clients, network observers, malformed provider responses, accidental app backups, and accidental disclosure in ordinary app flows. We do not claim protection from a compromised/rooted OS, a malicious provider/server the user configures, physical access to an unlocked device, or credentials intentionally disclosed by the user.

## Response and user controls

The host can be stopped to revoke volatile sessions. Android can remove its saved Windows credential or server configuration; local playback history/cache and downloads can be cleared independently. A provider may retain previously received requests. If the Windows token is removed while offline, stopping the host is the revocation action.
