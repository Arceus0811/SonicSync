# Security and privacy review

Review date: 25 October 2026. Scope: current repository Android implementation and optional Windows remote-media helper. This is a source review plus the specific build/tests listed in the delivery report, not a certification or comprehensive penetration test.

## Findings and changes in this pass

| Severity | Finding | Action/status |
|---|---|---|
| High | Android backup was enabled while app data included encrypted tokens and personal history/queue state. | Fixed: `allowBackup=false`, explicit cloud/device-transfer/full-backup exclusions. Tradeoff: users do not get automatic restore of app-private data. |
| Medium | Privacy disclosures and local cleanup controls were not available as a dedicated Settings destination. | Fixed: in-app Privacy & Security page with data flows, history/cache cleanup, download management, permission settings, provider removal, and Windows disconnect. |
| Medium | Embedded-player WebView had default file/content and mixed-content settings. | Fixed: disable file/content access and file-origin access, block mixed content, enable Safe Browsing. JavaScript remains enabled because the visible official player requires it; no JS bridge is present. |
| Medium | Subsonic auth token and salt are query parameters because this integration follows the Subsonic token-auth request scheme. | Documented residual exposure in the configured server's access/proxy logs; use only trusted HTTPS servers and protect their logs. Not falsely described as header-based. |
| Medium | Windows host used volatile bearer tokens and manual IP binding/local approval, but authenticated mutating message IDs were not replay-cached and tokens had no expiry. | Fixed: per-token bounded recent-ID replay rejection for control/disconnect, one-time pair code exercised in smoke test, bearer expires after 12 hours, host shutdown still revokes all sessions. |
| Low | Windows self-signed certificate identity relies on manual fingerprint comparison. | Documented. Android pins the exact certificate fingerprint and rejects changes. |

## Manifest and permissions

Declared by the app and merged libraries: `INTERNET`, `READ_MEDIA_AUDIO`, legacy `READ_EXTERNAL_STORAGE` capped at API 32, `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_MEDIA_PLAYBACK`, `POST_NOTIFICATIONS`, `ACCESS_NETWORK_STATE`, and `WAKE_LOCK`. Launcher `MainActivity` and media-session `PlaybackService` are exported for their respective Android contracts; `YouTubeEmbedActivity` is not exported. Custom Media3 commands are package-gated by source code. No runtime microphone, location, contacts, camera, phone/SMS, VPN, accessibility, or device-admin permission is declared.

## Network and listener review

Android server configuration requires HTTPS, rejects URL userinfo, disables redirects, and bounds response size/time. Jellyfin token is sent in `X-Emby-Token` for API requests and only attached to matching-host stream requests. The Windows helper is manually launched; explicit listen address and port; no auto firewall or router configuration. It supports TLS 1.2+, short-lived pairing code, approval prompt, token auth, 12-hour session expiry, replay rejection for mutating requests, limited verbs, fixed process arguments, and bounded request JSON. Default port 47931; user can choose another permitted port. The helper has no user-facing Settings screen.

## Credentials and logging

Android server configs use AES/GCM with Android Keystore-backed key. Jamendo client ID is public API configuration, not a credential; Jellyfin device ID is random and non-secret. No Google passwords/cookies are requested. Windows PFX password is DPAPI protected for current Windows user; pairing tokens are memory-only. Source review found no explicit logging of authorization headers or provider passwords. This is not proof that every dependency or OS diagnostic excludes all sensitive material. Subsonic query credentials can enter the configured server's URL logs.

## Dependencies, scans, and remaining work

The Android app uses AndroidX/Media3/Compose/Kotlin dependencies and no analytics/crash-upload SDK was identified in the Android module dependency list. No dependency vulnerability scanner, secret-scanner, formal threat assessment, or third-party penetration test was run in this review. Add these to release CI before public distribution. Review dependency licenses before redistributing APKs. A public privacy policy URL is still needed for store distribution; the repository and in-app text are not a published URL.

The loopback Windows helper smoke script was run successfully on 25 October 2026. It verified TLS certificate fingerprint pin comparison, health, unauthenticated rejection, bad-code rejection, locally approved one-time pairing, authenticated state, invalid command rejection, replay rejection, disconnect/revocation, and shutdown. It did not use a phone or change Windows audio routing.

Remaining follow-up: add a clear-all library control for playlists/favorites if requested (history and cache are separately clearable, downloads separately manageable), test the Android screen on a physical device, and review redirect/DNS-rebinding behavior for private self-hosted servers without breaking legitimate local use.
