# SonicSync virtual render endpoint (development target)

This folder contains a narrowly configured derivative of Microsoft's SysVAD
sample. Its package is configured to register one virtual **render** endpoint
named `SonicSync Virtual Output`; endpoint enumeration has not been verified
because the package has not been installed. The driver is a Windows audio endpoint only; the existing SonicSync
user-mode relay remains responsible for WASAPI loopback capture, portable DSP,
and rendering to a separately selected physical output.

## Build without installation

Prerequisites on Windows x64:

- Visual Studio 2022 Build Tools with MSVC v142 x64/x86 tools, ATL, and the
  Windows Driver Kit BuildTools component.
- Windows SDK 10.0.26100.0.
- Microsoft NuGet packages `Microsoft.Windows.WDK.x64` and
  `Microsoft.Windows.WDK.x86` 10.0.26100.6584, in the standard per-user NuGet
  package cache.
- PowerShell and `vswhere.exe` from Visual Studio Installer.

Configure SonicSync with CMake as usual, then build the isolated driver target:

```powershell
cmake --build build --target sonicsync_virtual_endpoint --config Debug
```

The target stages source under `build/driver/source`, uses 64-bit MSBuild,
keeps WDK package verification enabled, and writes a log and unsigned
artifacts under `build/driver`. It does not sign, install, enable test mode,
change default audio devices, or otherwise modify Windows audio routing.
Compilation and package validation are not proof of endpoint operation.

The separate `sonicsync_virtual_endpoint_relay` target is the user-mode relay
for this device. It rejects a source whose endpoint name does not contain
`SonicSync`, rejects a SonicSync-named destination, and checks that source and
destination endpoint IDs differ. Choose a physical output when starting it;
the current endpoint API does not certify that an arbitrary non-SonicSync
render endpoint is physical. Applications must be explicitly routed to the
virtual endpoint; Windows default output is not changed automatically. If the
relay is stopped, apps still routed to the virtual endpoint will be silent
until routed back. The generic `sonicsync_wasapi` target is preserved.

## Status and safety

This source has not been installed or exercised as a Windows endpoint. Its
package is unsigned. Do not install it on a daily-use PC. A supported trusted
signature, review of generated INF/catalog, disposable-machine installation,
measured end-to-end audio tests, and reviewed recovery procedure are still
required. Do not enable test-signing mode or weaken Windows driver security.

Only the render miniport pair is registered. The sample's capture endpoints,
APOs, and keyword detector are not included in the package. The SysVAD
implementation still contains upstream source files needed by its shared
miniport infrastructure; this target is a buildable engineering slice, not a
production-quality released driver.

## Provenance and license

Derived from Microsoft Windows Driver Samples, SysVAD, source commit
`bb68ed5e770bce04dc5395ec9258dafab034184d`. The upstream MIT license is
preserved in `LICENSE.WINDOWS-DRIVER-SAMPLES.txt`. See the project investigation
document for signing, installation, rollback, testing, and routing constraints.
