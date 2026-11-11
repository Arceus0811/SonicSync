# Android to Windows remote media control

## Available capability

The opt-in Windows host exposes protocol v1 over HTTPS and delegates playback state and transport commands to the current Windows Global System Media Transport Controls (GSMTC) session. Supported commands are play, pause, toggle, next, and previous. Track title, artist, album, source application, and play state are returned when the active media session publishes them. This controls the active media session; it does not select a specific browser tab or guarantee that every application publishes a session.

This feature does not transport PC audio to Android, modify the Windows audio route, control SonicSync's WAV relay, change Windows volume, or synchronize the SonicSync local WAV library/playlists. The Windows app has no stable library API, so library and playlist sync are intentionally absent.

## Start it only when needed

Build the Windows Release targets and install Node.js 20 or newer. No npm packages are used. Pick an IP address assigned to the PC with `Get-NetIPAddress`; use a private Wi-Fi IPv4 address for a phone, or `127.0.0.1` for a local-only check. The helper binds only to the exact address supplied.

From a normal (non-administrator) PowerShell window in the project root:

```powershell
cmake --build build --config Release --target sonicsync_media_session_remote
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Start-SonicSyncRemoteHost.ps1 -ListenAddress 192.168.1.24 -Port 47931
```

Replace the address with the PC's private address. The script creates a self-signed TLS certificate directly in `%LOCALAPPDATA%\SonicSync\RemoteHost`; the private key stays in the password-protected PFX file, whose random password is protected with Windows DPAPI for the current user. It does not import a certificate into a Windows trust store, require elevation, change the default audio device, or create a firewall rule. The console displays the certificate SHA-256 fingerprint and a five-minute pairing code. The remote host remains in the foreground. Close it with Ctrl+C to stop serving and revoke every in-memory pairing token. Each token expires after 12 hours; authenticated mutating requests reject repeated request IDs within a bounded recent-ID cache.

If Windows Firewall blocks phone-to-PC traffic, add a narrowly scoped **Private network** inbound rule for the selected port using Windows Security after reviewing it. SonicSync never creates this rule. Do not expose the port on Public networks or the internet.

In Android **Settings → Windows PC**, enter the PC address, port, displayed certificate fingerprint, and pairing code. Confirm the exact fingerprint against the Windows console. Windows then asks for local approval by device name; type `yes` to approve. The phone stores the resulting random token encrypted with the existing Android Keystore-backed store. Android checks the exact certificate pin on every HTTPS request; a changed certificate is rejected. The six-digit code is single-use, rotates after approval, and locks after five incorrect attempts for one minute. Each pairing token is held in Windows memory only and is invalidated at 12-hour expiry or when the host stops. Pair again after a host restart. Disconnect removes the phone's saved token; stop the Windows host to revoke that token immediately.

## Android emulator

For a host service bound to host loopback, Android Emulator commonly reaches the host through `10.0.2.2`; for a host bound to a LAN adapter, use that adapter's private IP. The certificate pin, not DNS name validation, authenticates the pinned self-signed server certificate. Pair only over a trusted local network and verify the fingerprint out of band.

Run the host-side transport/authentication smoke test with `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\Test-SonicSyncRemoteHost.ps1` after building. It creates a short-lived certificate in a temp directory, binds only to loopback on an available port, checks the TLS fingerprint, rejects unauthenticated state/control and a wrong pairing code, approves pairing locally, verifies a one-use code and repeated request-ID rejection, checks authenticated state/revocation, then stops the host and removes temporary files. It does not test a physical phone or a real external media session.

## Protocol and limits

`GET /v1/health` provides version-only health status. `POST /v1/pair` requires protocol version 1 and the short-lived code, followed by local Windows approval. `GET /v1/state` and `POST /v1/control` require a 256-bit bearer token. Request bodies are capped at 16 KiB; the host rejects unknown commands and has no anonymous control route. TLS 1.2 or later is required. No credentials or tokens are logged.

Pairing tokens are intentionally volatile rather than written to an unprotected Windows file. One running host accepts multiple explicitly approved devices with separate random tokens. Host state/control is implemented; catalog/library/playlist sync, spectrum streaming, remote SonicSync EQ control, and volume control are not implemented.

## Removal and recovery

Stop the endpoint with Ctrl+C. The endpoint does not change audio settings, install a driver, modify registry audio keys, or alter firewall policy. Remove `%LOCALAPPDATA%\SonicSync\RemoteHost` only after the service is stopped to remove the private certificate and its DPAPI-protected password. No host audio recovery action is required.
