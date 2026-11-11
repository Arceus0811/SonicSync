# SonicSync virtual audio endpoint investigation

Status: initial SonicSync SysVAD-derived render endpoint and separate
user-mode relay build targets implemented. The WDK NuGet toolchain and package
validation are usable. Driver artifacts are unsigned and have not been
installed or operated. No certificate, audio-routing, or boot-configuration
changes were made.

## Findings

### Existing SonicSync signal path

The current route is:

```text
external application
  -> selected Windows render endpoint
  -> WASAPI shared-mode loopback capture (AUDCLNT_STREAMFLAGS_LOOPBACK)
  -> SonicSync decode to float samples
  -> shared sonicsync::Processor (preset, EQ, limiter, bypass)
  -> bounded in-memory sample queue
  -> WASAPI shared-mode render to a separately selected render endpoint
```

The source and destination endpoint IDs must differ. The capture side reads
the source endpoint's mix format and currently accepts mono/stereo PCM 8/16/24/
32-bit or float32. Capture and render use event-driven shared-mode clients,
request a 20 ms buffer, and enable Windows PCM conversion. EQ controls are read
from the shared control block on capture packets. The current route does not
change the Windows default endpoint and does not mute the source endpoint.

Therefore, when the source is a physical speaker/headphone endpoint, the
unprocessed system mix is still played there while SonicSync renders a second
copy to the destination. This can produce doubled or delayed audio. The
current host lists four physical render endpoints and no virtual render
endpoint. The route is an explicit relay, not a virtual device and not
transparent system-wide processing.

With a proper virtual render endpoint selected as source, apps explicitly
routed to that endpoint, and a physical device selected as destination, the
desired topology is:

```text
external app -> SonicSync Virtual Output -> WASAPI loopback -> SonicSync DSP
  -> selected physical output
```

No application may also send the same audio to the physical output. SonicSync
must be running to forward sound; if it stops, the user must return the app's
route to a physical endpoint. Neither relay has verified automatic hot-unplug
recovery.

### SonicSync endpoint derivative and distinct build targets

The project now contains `drivers/sonicsync-virtual-endpoint/sysvad`, a
reviewed development derivative of Microsoft SysVAD commit
`bb68ed5e770bce04dc5395ec9258dafab034184d`. The package INF uses the unique
`Root\SonicSyncVirtualAudio` ID and SonicSync service/driver/catalog names. The
runtime miniport list contains one render pair; capture filter installation is
disabled in the derived adapter, and the INF registers only its render wave and
topology interfaces. Sample APOs and the keyword detector are excluded from
the package. Upstream files and the Microsoft license are retained.

Build separation:

- `sonicsync_virtual_endpoint`: stages the driver source below the CMake build
  folder, builds `EndpointsCommon` and the driver package with 64-bit MSBuild,
  and keeps WDK package verification enabled. The artifacts are unsigned.
- `sonicsync_virtual_endpoint_relay`: builds the existing WASAPI relay with a
  source-name guard requiring a SonicSync-named render endpoint, a
  non-SonicSync destination name, and distinct endpoint IDs. The user must
  select the physical destination. Endpoint name alone cannot prove that a
  third-party render endpoint is hardware.
- `sonicsync_wasapi`: the original generic relay remains intact.

Rebuild both targets and the existing tests with:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --target sonicsync_virtual_endpoint sonicsync_virtual_endpoint_relay --config Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The WDK package build is run with `SkipPackageVerification=false`,
`SignMode=Off`, and `EnableTestSign=false`. For an additional strict INF check,
run the cached WDK `infverif.exe /h` against
`build\driver\source\Package\x64\Debug\package\SonicSyncVirtualAudio.inf`.
The build's Signability test reported no errors/warnings and generated the
catalog; direct strict INF validation exited 0. These are static build/package
checks only.

### Local Windows driver toolchain

Observed in this execution environment on 2026-10-21:

| Component | Status | Evidence |
|---|---|---|
| Visual Studio 2022 Build Tools | Present, 17.14.37806.7 | `vswhere` found the installation |
| MSVC x64/x86 tools | Present: v143 14.44.35207 and v142 14.29.30133 | VS Build Tools contains both toolsets; v142 Spectre x64 libraries and ATL are present |
| Windows SDK | Present, build 26100 | Windows SDK 10.0.26100.0 headers/libs and `signtool.exe` are present |
| WDK kernel headers/libraries | Present via NuGet | `Microsoft.Windows.WDK.x64` 10.0.26100.6584 installed in the user NuGet cache, including `portcls.h` and WDK build targets |
| WDK Visual Studio integration | VS BuildTools WDK BuildTools component present; full standalone WDK setup integration incomplete | `vswhere -requires Component.Microsoft.Windows.DriverKit.BuildTools` found VS 2022 Build Tools. WDK MSBuild targets are loaded from the supported NuGet package. The standalone WDK setup bootstrapper did not finish its machine-wide integration. |
| WDK packaging/validation tools | Present via NuGet and exercised | Enabled WDK package verification, Inf2Cat/signability, and direct `InfVerif /h` are exercised on the SonicSync-derived package. Generated SYS/CAT remain unsigned. |
| SysVAD source and WIL submodule | Official source checkout and a trimmed derivative are present | Microsoft sample checkout under `%TEMP%` is retained; the SonicSync driver source is copied into this repository with upstream license. WIL is unnecessary for the selected driver-only projects. |
| Windows ADK / HLK | Not established | Not required to inspect or compile the sample; required for relevant Microsoft driver submission/certification workflows and not verified installed |
| Hardware Developer Program / EV signing certificate | Not established | Account and certificate availability cannot be determined from local tool paths |
| PnPUtil | Present | Windows inbox `pnputil.exe`; do not use it to install anything during this investigation |
| Virtual audio endpoint | Missing | SonicSync WASAPI enumeration showed only the four physical outputs |

The supported WDK NuGet route is usable for compilation and package
verification, but does not establish a complete signed deployment workflow.
No driver signing certificate or Microsoft Hardware Dev Center account was
configured. The downloaded WDK bootstrapper was Microsoft-signed, but its
legacy setup dependency attempted an HTTP download blocked by the host network
policy; setup therefore did not finish its machine-wide integration. No
driver was installed and no Windows security or routing settings changed.

### Toolchain install and SysVAD build verification (2026-10-21)

Installed developer components:

- Visual Studio 2022 Build Tools 17.14.37806.7 / MSBuild 17.14.60.
- MSVC v143 14.44.35207 plus v142 14.29.30133; v142 ATL and Spectre x64
  libraries were selected in Visual Studio Installer.
- Windows SDK 10.0.26100.0.
- Official NuGet packages `Microsoft.Windows.WDK.x64` and `.x86`
  10.0.26100.6584, plus SDK C++ package dependencies, in
  `%USERPROFILE%\.nuget\packages`.
- Official Microsoft Windows Driver Samples repository and its WIL submodule
  in `%TEMP%\sonicsync-Windows-driver-samples`.

The WDK setup EXE was verified with a valid Microsoft Authenticode signature.
It could not finish because it attempted to retrieve a setup MSI over plain
HTTP, which this host blocks. NuGet is Microsoft's supported alternative for
WDK 10.0.26100 packages. The downloaded installer did not install a driver or
change Windows security or audio settings.

The official SysVAD solution was rebuilt in the temporary checkout for x64
Debug using MSBuild 17.14, the v142 14.29.30133 toolset, SDK/WDK build 26100,
and `SignMode=Off` / `EnableTestSign=false`. The final verification-enabled
build succeeded with 8 MSB8029 warnings and 0 errors; the warnings only note
intermediate/output paths beneath `%TEMP%`. WDK signability reported no errors
or warnings and generated the sample catalog. The `.sys` and `.cat` are
unsigned. A direct strict `InfVerif /h` run found one upstream SysVAD INF issue
(`MsApoFxProxy.Registration` needs reference); the SonicSync derivative below
includes the required `Needs` entry and passes the same strict check.

An earlier configure attempt used the wrong WDK root and failed to resolve
`portcls.h`; a 32-bit MSBuild attempt also failed loading the verifier DLL.
Those invocation mistakes were resolved by using the package's `c\` root with
a trailing separator, matching SDK/WDK build numbers, and 64-bit MSBuild. The
final builds use `SkipPackageVerification=false`, `SignMode=Off`, and
`EnableTestSign=false`. No validation was disabled to obtain success.

Reproducible setup route for the already cached packages (developer tools
only):

```powershell
nuget install Microsoft.Windows.WDK.x64 -Version 10.0.26100.6584 -OutputDirectory $env:USERPROFILE\.nuget\packages
nuget install Microsoft.Windows.WDK.x86 -Version 10.0.26100.6584 -OutputDirectory $env:USERPROFILE\.nuget\packages
git clone https://github.com/microsoft/Windows-driver-samples.git $env:TEMP\sonicsync-Windows-driver-samples
git -C $env:TEMP\sonicsync-Windows-driver-samples submodule update --init
```

The SysVAD build used the official sample solution, target `Debug|x64`,
`VCToolsVersion=14.29.30133`, SDK/WDK build `10.0.26100.0`, and the WDK
NuGet `WDKContentRoot`/tool paths. For a reproducible signed or installable
package, first complete the official WDK integration, run INF/package
validation with it enabled, and use an approved signing path. This work did
not sign or install the result.

## Architecture evaluation

### Recommended implementation path

Implement a SonicSync-branded **virtual render endpoint** and keep the DSP in
the existing user-mode SonicSync engine for the first version. Route only
applications explicitly selected by the user to that endpoint. The bridge
captures the virtual render mix with WASAPI loopback, processes it through the
existing portable DSP, and writes it to the chosen physical endpoint. This is
less kernel-mode DSP code and gives a single existing, testable DSP path.

Build the virtual endpoint using Microsoft SysVAD as a reference foundation,
but make a purpose-built, minimal render endpoint rather than shipping the
sample unchanged. Adapt its INF/device identity, endpoint names, supported
formats, WaveRT render behavior and lifecycle/resource handling; remove
unneeded sample endpoints/APOs; and validate that ordinary application audio
submitted to its render endpoint is returned by WASAPI loopback. The sample's
loopback/capture demonstrations and sample APOs are not proof that it already
behaves as an audio cable. The critical acceptance test is an actual routed
external application, not a sample-generated tone.

Keep the current bridge's source and physical destination explicit. Add an
endpoint-role check (SonicSync virtual render source, distinct physical render
destination), clear routing status, a stop action, and no automatic default
device changes. Do not feed the physical destination back into the virtual
source. Show the recovery action if the relay exits. Consider a later optional
APO path only after the user-mode endpoint route is stable and measurable.

### Alternatives

| Approach | Evaluation |
|---|---|
| WASAPI loopback on physical output | Already exists, but the original signal remains audible. Unsuitable as the external-app solution. |
| Existing third-party virtual cable + SonicSync relay | Fastest way to validate app routing and DSP before owning a driver. It adds a separate vendor driver, separate license/update/removal path, and would not be a SonicSync-owned endpoint. No package was acquired or installed. |
| SysVAD-derived SonicSync virtual render endpoint + user-mode relay | Chosen long-term first endpoint architecture. Provides a real Windows render endpoint and reuses the current DSP. Requires nontrivial WDM/WaveRT/INF work and actual loopback validation; SysVAD is a sample, not a ready-made pass-through cable. |
| APO associated with the physical endpoint | Can run inside the Windows audio graph for an associated endpoint and avoids the extra user-mode capture/render hop, but requires an APO DLL, endpoint/componentized INF association and driver-style package integration. It is endpoint/OEM-package-specific and has higher deployment and servicing risk. Not selected for the first working route. |
| Kernel-mode DSP inside a custom virtual driver | Not selected. Duplicates the portable DSP, increases kernel crash/security exposure, and makes DSP iteration harder. |

Microsoft documents SysVAD as a WDM/WaveRT virtual audio sample with multiple
endpoint examples and sample APOs. Its current build documentation requires
Visual Studio, the Windows SDK and WDK, plus v142 MSVC/ATL tools and the Windows
Implementation Library (WIL) submodule. Microsoft's deployment instructions
for the sample explicitly use test signing and warn that boot/security settings
may need adjustment. The sample should be narrowed and validated, not installed
unchanged. See [SysVAD sample and build/deployment instructions](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/sysvad-virtual-audio-device-driver-sample/).

## Toolchain installation notes (build tools only)

These are not system audio-driver installation steps. They install development
tools only and do not install or activate an audio endpoint.

1. The following components are now installed: **Visual Studio Installer → Modify → Individual components** for the
   existing VS 2022 Build Tools. Keep the existing Desktop development with C++
   and Windows 11 SDK components. Add the current SysVAD prerequisites listed
   by Microsoft: **MSVC v142 - VS 2019 C++ x64/x86 build tools**, **C++ ATL for
   latest v142 build tools x64/x86**, and the corresponding **Spectre-mitigated
   libraries**. These are needed because SysVAD's published sample build
   instructions currently target v142, while this host has v143.
2. The standalone WDK MSI integration remains incomplete because its signed
   bootstrapper could not retrieve a setup dependency over HTTP. This is not a
   blocker for the supported 10.0.26100.6584 NuGet build path documented below;
   do not use unofficial mirrors. SDK and WDK build numbers match (26100).
3. The official Microsoft `windows-driver-samples` source is in a separate
   development folder, then initialize its WIL submodule:

   ```powershell
   git clone https://github.com/microsoft/Windows-driver-samples.git C:\src\Windows-driver-samples
   cd C:\src\Windows-driver-samples
   git submodule update --init
   ```

   The sample documentation directs using `git submodule update --init` at
   repository root; WIL is included as a submodule dependency.
4. In VS 2022 Developer PowerShell, verify the toolchain before any driver
   build:

   ```powershell
   $kits = 'C:\Program Files (x86)\Windows Kits\10'
   Get-ChildItem "$kits\Include" -Directory | ForEach-Object {
       Get-ChildItem (Join-Path $_.FullName 'km') -ErrorAction SilentlyContinue
   }
   Get-ChildItem "$kits\Lib" -Directory | ForEach-Object {
       Get-ChildItem (Join-Path $_.FullName 'km') -ErrorAction SilentlyContinue
   }
   Get-ChildItem "$kits\Tools" -Recurse -File -ErrorAction SilentlyContinue |
       Where-Object Name -in 'Inf2Cat.exe','DevCon.exe','Stampinf.exe','signtool.exe'
   $wdk = "$env:USERPROFILE\.nuget\packages\microsoft.windows.wdk.x64\10.0.26100.6584"
   Get-ChildItem "$wdk\c" -Recurse -File -ErrorAction SilentlyContinue |
       Where-Object Name -in 'Inf2Cat.exe','InfVerif.dll','stampinf.exe','drvcat.exe'
   Get-Command msbuild.exe, cl.exe, signtool.exe
   ```

   The NuGet package stores WDK headers/tools below its `c` subfolder, not in
   the standard Windows Kits directories, so also inspect the package path.
   A successful compile/package build is not a deployment approval: package
   validation, a trusted signature, and external-app route testing remain
   mandatory.

Microsoft's current WDK download page says use WDK 26100.6584 when staying on
VS 2022 and requires matching SDK/WDK build numbers. The sample build page calls
out v142 and WIL. See [WDK download/version guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk), [supported WDK versions](https://learn.microsoft.com/en-us/windows-hardware/drivers/other-wdk-downloads), and [SysVAD prerequisites](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/sysvad-virtual-audio-device-driver-sample/).

## Signing, installation, and permissions

- Driver package installation creates a PnP device and stages an INF in the
  Windows Driver Store. It requires administrator elevation. A device install
  or removal may restart audio services or affect audio sessions, so do it only
  in an announced maintenance window with all work saved.
- Local development/test signing is not a safe route for this daily-use PC.
  Microsoft's SysVAD manual instructions require test-signing configuration;
  Microsoft's guidance warns this can require Secure Boot/BitLocker changes.
  No test-signing mode, boot configuration or security feature will be changed.
- For a retail-quality driver, obtain an EV code-signing certificate and
  Hardware Developer Program account, package the driver and submit through
  Microsoft's Hardware Dev Center. HLK/WHCP testing and dashboard signing is
  Microsoft's recommended production route. Attestation signing requires EV
  and Hardware Developer Program access, is not Windows Certified, and its
  current guidance limits it to testing scenarios rather than publishing retail
  drivers on Windows Update. A Microsoft-signed package must be verified before
  installation. No certificate/account availability was inspected.
- The development-tool install above is independent of driver signing and
  system audio configuration. Do not install a build-generated driver unless
  it has a supported trusted signature and the test gate below is met.

See [Microsoft driver-signing options](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/driver-signing-offerings), [attestation signing prerequisites/limits](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-attestation), and [test-signing requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/test-signing).

## Installation, rollback, and uninstall plan

Deployment is a later, explicitly approved step. No install command is to be
run now.

1. Build only from the official sample source plus reviewed SonicSync changes.
   Record the source commit, INF hardware ID, package version and SHA-256 hashes.
2. Run static INF validation, package/catalog generation, signature
   verification, and audio endpoint/DSP tests in a disposable Windows 11 x64
   VM or spare test PC. Use a properly signed driver accepted by that test
   environment; do not relax Secure Boot, kernel signing or BitLocker policy.
   If a trusted signed test package cannot be obtained, stop before device
   installation.
3. Before installation, record the current default playback endpoint and every
   per-app Volume Mixer assignment. Keep the original physical output selected
   as Windows default. Stage/install only the SonicSync virtual endpoint; do not
   set it as default automatically. The user manually routes one test app to it.
4. Verify endpoint appears in Windows Sound and SonicSync's endpoint list. Run
   the route from the virtual endpoint to a different physical endpoint. Check
   only that external app is audible at the physical target. Ensure there is no
   second copy audible from any original physical route.
5. Rollback: stop the SonicSync route; explicitly route test apps back to the
   recorded original physical output; confirm direct playback; then remove the
   virtual PnP device and its exact OEM INF/package using Windows Device
   Manager/PnPUtil. Do not delete unrelated audio drivers. Verify the default
   endpoint and per-app assignments match the recorded state, then reboot only
   if the driver package requests it.
6. Uninstall: the SonicSync application uninstaller must stop its relay, remove
   the SonicSync virtual device/package, and leave the user's existing default
   and per-app routes untouched. If device removal fails, provide a clear
   administrator procedure using the recorded device instance ID and OEM INF;
   never use a broad `pnputil /delete-driver` command.

The installation UI must state that apps routed to the SonicSync endpoint
become silent if SonicSync exits. Recovery is to stop/remove the SonicSync
route and select the saved physical device for the affected apps. The product
must never silently switch the global default device.

## External-application verification plan

1. Install only on a disposable Windows 11 x64 test system after the package
   has a supported trusted signature. Keep Windows default output physical.
2. Route one known external player (initially a simple media player, then
   Spotify and a browser session) to SonicSync Virtual Output in Windows
   Volume Mixer. Confirm the app is actively playing and SonicSync sees
   non-silent loopback samples.
3. Use deterministic tones played by that external app at 60 Hz, 100 Hz,
   1 kHz and 8 kHz. Measure pre/post DSP RMS or Goertzel/FFT amplitude on the
   captured and rendered signal. Verify the selected bass/treble EQ changes
   the expected bins by its configured dB within tolerance; verify Neutral and
   bypass match the virtual endpoint input within the bridge's expected
   conversion tolerance.
4. Change profiles during playback and verify there is no playback restart,
   dropout or discontinuity beyond the documented smoothing transition.
5. Disconnect/re-enable the physical output during playback. Verify the relay
   reports endpoint invalidation, does not crash or loop audio, recovers only
   after explicit retry/reselection, and never changes the default endpoint.
6. Stop SonicSync. Confirm the test app becomes silent while still routed to
   the virtual endpoint, then route it back to the saved physical endpoint and
   verify playback restores. Repeat uninstall and confirm endpoints/routes
   return to the recorded state.
7. Repeat for Spotify and browser media. Media-session metadata is separate
   from audio routing and does not count as proof that the player's samples
   reached DSP.

## Current milestone boundary

Verified from local inspection: the existing route remains WASAPI loopback on a
selected render endpoint, then user-mode DSP, then a separately selected output;
the original physical source remains audible. There is no installed virtual
endpoint. A SonicSync-branded SysVAD-derived driver now exists in a separate
source/build target and exposes only a render miniport pair. It built with
64-bit MSBuild and verification enabled; WDK signability reported no issues,
and generated INF passed direct `InfVerif /h`. The existing CMake application
targets built in Debug and both existing CTest tests passed. The driver SYS/CAT
are unsigned. This work did not install a driver, alter audio routing, change
boot/security settings, terminate SonicSync, or replace installed executables.

Verification in this host:

- CMake Debug build completed for `sonicsync_gui`, `sonicsync_player`,
  `sonicsync_wasapi`, `sonicsync_virtual_endpoint_relay`, `dsp_benchmark`, and
  both test executables.
- CTest: `dsp_tests` passed and `windows_artwork_tests` passed (2/2, 0 failed).
- Driver package: x64 Debug compile succeeded; WDK signability completed with
  `Errors: None`, `Warnings: None`; catalog generated; `InfVerif /h` exited 0.
- Relay `--list` ran read-only and returned the same four physical outputs; it
  confirms that no SonicSync endpoint is currently installed. It does not test
  virtual endpoint operation.
- Driver artifacts under `build\driver\source\Package\x64\Debug\package`
  are the INF, SYS, and CAT. Signing was explicitly off. No external-app audio
  route or measured end-to-end EQ/bypass test was possible without installing
  the driver on a disposable test system.
- Package hashes are recorded in the latest review section below. Do not rely
  on hashes from earlier builds.

Rebuild the isolated driver package from the repository root with:

```powershell
cmake --build build --target sonicsync_virtual_endpoint sonicsync_virtual_endpoint_relay --config Debug
```

Output and log are under `build\driver`; source is staged under
`build\driver\source`. `sonicsync_wasapi` remains the generic relay. The
endpoint and dedicated relay have not yet been operated together.

Still unverified: actual endpoint enumeration/installation, audio routing from
external apps through this driver, loopback sample delivery, measured EQ and
bypass at a physical output, duplicate-audio prevention on the host, and
endpoint hotplug recovery. Driver signing and Microsoft submission readiness
are also unknown. Do not install these artifacts.

## Integration review and latest verification (2026-10-21)

### Signal-path review

The driver source configures `g_RenderEndpoints` with the SysVAD
`SpeakerMiniports` pair, sets the capture endpoint count to zero, and the INF
registers only the WaveRender and TopologyRender interfaces. That is a virtual
render endpoint design with a WaveRT render path and loopback pin in the
underlying miniport tables. Static inspection supports WASAPI loopback as the
documented capture interface, but only installation and endpoint operation
can prove Windows enumerates it and returns the submitted stream.

The dedicated relay's concrete path is: endpoint enumeration by
`IMMDeviceEnumerator` -> source `IAudioClient` initialized in shared-mode
loopback -> decode samples -> `Processor::process` -> bounded queue -> a
second shared-mode `IAudioClient`/`IAudioRenderClient` -> chosen render device.
The UI/network/disk are not called from this relay loop. The relay supports
mono/stereo PCM 8/16/24/32-bit and float32; it rejects other channel counts or
formats. Shared-mode `AUTOCONVERTPCM` and `SRC_DEFAULT_QUALITY` allow Windows to
convert the stream for the render endpoint when needed. Runtime format
negotiation and actual playback remain untested.

Known relay limitations:

- There is no device-notification/reconnection path. A removed or invalidated
  endpoint causes the route to stop; the user must restore the app route and
  explicitly restart/reselect devices.
- The dedicated-target guard identifies virtual source and destination by
  friendly-name substring plus distinct endpoint IDs. A non-SonicSync name is
  not proof that a destination is physical. The user must select a known
  physical output; robust role classification is not implemented.
- No loopback/render measurement harness exists in the relay yet. Its peak
  logs are diagnostics, not frequency-response evidence.
- App audio reaches the virtual source only when that app is explicitly routed
  to the endpoint. The relay does not change the Windows default and does not
  suppress audio that an app separately sends to another endpoint.

### Build, package validation, and reproducibility

The workspace did not expose CMake on `PATH` in this invocation. I used the
installed Visual Studio CMake executable directly:

```powershell
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake --build build --target sonicsync_virtual_endpoint sonicsync_virtual_endpoint_relay --config Debug
& $cmake --build build --config Debug
& $cmake --build build --target RUN_TESTS --config Debug
```

Results on 2026-10-21:

- The isolated x64 Debug driver target compiled successfully. WDK package
  signability ran with `SkipPackageVerification=false`, reporting no errors
  and no warnings; catalog generation completed. The script then ran strict
  `InfVerif /h`, which exited successfully. No validation was skipped.
- The dedicated relay and complete existing Debug application targets built.
- CTest ran `dsp_tests` and `windows_artwork_tests`: 2/2 passed, 0 failed.
- Repeated package builds initially showed the SYS changing due to its PE
  timestamp. The driver project's linker now uses `/Brepro`; two subsequent
  clean staging/build invocations produced identical SYS SHA-256 and identical
  INF SHA-256. The CAT still changed between runs despite the stable SYS and
  INF; the cause has not been isolated, so the full package is not bit-for-bit
  reproducible yet. Validation is repeatable, but identical CAT output is not
  established.
- Latest package: `SonicSyncVirtualAudio.sys` (433,664 bytes), SHA-256
  `56A1B71625287D67527AE131E54AF08BA66C7D7D0C652E37998758EE76BA2538`;
  `SonicSyncVirtualAudio.inf` (3,925 bytes), SHA-256
  `7921C71F37F808B77869B77B5FD4B6ED2791A87A31320A5124D1826CC6D285A7`;
  `sonicsyncvirtualaudio.cat` (5,569 bytes), SHA-256
  `1C3E3E873F67F3311092F61624D38EE91EA9A540C39324A638CCE600F57D5CA7`.
- The SYS and CAT were generated with signing off. `signtool verify /kp /v`
  returned `No signature found` for both (exit code 1). Build success and
  catalog generation do not make this package installable under normal driver
  signature enforcement.

### Runtime test gate and outcome

No driver was installed in a VM or spare PC during this review. The host has
not supplied a trusted signed package or a disposable Windows 11 x64 test
environment. Thus endpoint enumeration, external-app routing, signal delivery,
EQ/bypass measurement, duplicate-audio behavior, and disconnect/recovery have
not been executed. The current host still lists four physical outputs only.

Before runtime testing, use a disposable Windows 11 x64 VM or spare PC with
normal Secure Boot and driver-signature policy retained. Obtain a Microsoft
Hardware Dev Center signed package through an approved signing/submission
workflow; a local self-signed package or an unsigned NuGet build is not enough
for this gate. Package installation/removal requires administrator elevation.
Record the default output and per-app Volume Mixer routes, leave the default
output physical, and explicitly route one test app to SonicSync Virtual Output.
Start the relay to a different known physical output, measure captured versus
rendered RMS/FFT for known tones, switch to a measured EQ preset and bypass,
check for a single audible stream, then test playback/profile changes and
physical-device removal/re-enable. Finally stop the relay, route the app back
to the recorded physical endpoint, verify restored playback, and remove only
the recorded SonicSync device/OEM package. If the required Microsoft-signed
package is unavailable, do not install and do not relax the test machine's
security policy.
