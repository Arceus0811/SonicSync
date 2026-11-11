# SonicSync isolated VM host assessment

Assessment date: 2026-10-21. No hypervisor was installed, no VM was created,
and no Windows audio, driver, boot, or security settings were changed.

## Recheck after user confirmed UAC availability

The current execution still runs as the non-admin sandbox identity
`Lucas\CodexSandboxOffline`; the confirmation that the user can approve UAC
does not make a host UAC dialog accessible to this execution. No existing
VMware/VirtualBox installation or installer was found. The only `.iso` found
on E: was an unrelated Adobe Premiere ISO; there is no Windows 11 ISO in the
checked Downloads/E: locations. No VMX/VMDK/VHDX/OVA/OVF files were found in
the likely VM directories checked. Existing SonicSync Debug/Release executables
and the unsigned driver package remain under the repository's `build` folder.

At recheck, C: had 50.3 GB free, E: had 100.0 GB free, and the host had 3.2 GB
available RAM (15.3 GB total). The Windows API again reported firmware
virtualization enabled, SLAT false, and no active hypervisor. The Home edition
cannot host the full Hyper-V role. The VMware Workstation installer is not
present, and no package manager (`winget`, Chocolatey, or Scoop) was found on
PATH. No installation or download was attempted. These measurements are a
snapshot and must be rechecked after freeing host RAM and before VM creation.

This environment cannot display or interact with host UAC, and its writable
scope excludes E:\. Installing Workstation and creating VM files therefore
cannot be performed safely in this turn. The exact manual host step is:

1. Download VMware Workstation Pro from Broadcom's official portal using its
   current installer, then run that installer interactively as Administrator.
   The prompt authorizes installation of the VM application, hypervisor
   drivers/services, and virtual networking components; a reboot may be
   requested. Keep host Secure Boot, driver-signature enforcement, and audio
   settings unchanged. The current Broadcom license notice says Workstation
   Pro is free across personal, educational, and commercial use; verify the
   download page and license presented by the installer before proceeding.
2. After installation/reboot, ensure at least 8 GB host RAM is available for
   the proposed 4 GB guest while keeping the host responsive. E: currently has
   100 GB free; preserve at least 20 GB for the host and avoid snapshots until
   free-space impact is reviewed.
3. Return to this task. The next step is to verify the installed `vmrun.exe`,
   recheck disk/RAM and firmware/SLAT, then request the workspace-sandbox
   approval needed to create `E:\SonicSync-VMs\SonicSync-Windows11-Test`.
   Obtain the official Microsoft Windows 11 x64 ISO only after those checks.

## Host facts available to this execution

This is the current Codex execution environment, running as
`Lucas\CodexSandboxOffline` without administrator privileges. That identity
does not match the repository owner's Windows profile. WMI/CIM and
`systeminfo.exe` returned Access denied; online DISM feature inspection
returned error 740 (elevation required). Thus some physical-host facts could
not be independently inspected.

| Item | Observed result |
|---|---|
| Windows | Registry reports `Windows 10 Home Single Language`, display version `25H2`, build `26200.9168`; Microsoft's release table identifies build 26200 as Windows 11 25H2. |
| Architecture | AMD64/x64 |
| CPU | AMD Ryzen 5 5600H with Radeon Graphics; 12 logical processors exposed |
| RAM | 15.3 GB total; 3.0 GB available during inspection |
| Virtualization | Windows `IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED)` returned true. The SLAT feature query returned false; because WMI/systeminfo are inaccessible, SLAT is not treated as verified. |
| Hypervisor | `HypervisorPresent` feature query false. `hvhost` is running, but Hyper-V management binaries/services were not found. |
| Hyper-V | The reported Home edition does not support the full client Hyper-V role. Windows optional-feature state could not be queried without elevation. |
| VMware / VirtualBox | Expected executable paths absent; uninstall registry search found no matching product. |
| Disk free | C: 42.3 GB; E: 108.0 GB. A Windows 11 guest requires at least a 64 GB virtual disk, before accounting for ISO, toolchain, checkpoints, and host reserve. |
| Utilities | PowerShell 5.1, DISM, and BCDEdit are present. DISM/system configuration queries that require elevation are unavailable to this process. |
| SonicSync build | `build` contains 659 files totaling about 0.2 GB, including the existing app, relay, test, and unsigned driver package outputs. |
| Permission | Current process is not an administrator. |

## Decision and blockers

No usable hypervisor was detected. Hyper-V is not a fit for this Home edition.
VMware Workstation Pro is a practical candidate because Broadcom currently
offers the desktop hypervisor without charge across personal, educational, and
commercial use; VirtualBox is another possible host. Both require a host-level
installation, which may install drivers/services and require a restart. The
attached request requires additional approval for a significant host change,
and this process cannot elevate. No hypervisor installation or download was
attempted.

Memory is also insufficient for a safe test run right now: Windows 11 guests
require at least 4 GB of guest RAM, but only 3 GB was available to the host
during inspection. Disk E: has room for an evaluation ISO and a dynamically
allocated VHD, but reserve is limited once Windows, Visual Studio, WDK, and
snapshots are added. Recheck both resources immediately before provisioning.

The host's Windows build is Windows 11 25H2 despite the legacy `ProductName`
registry value saying Windows 10. The registry identity alone should not be
used to select an ISO. Microsoft offers a multi-edition Windows 11 x64 ISO;
an Enterprise 90-day evaluation is another official option for eligible
evaluation use. Windows activation/licensing must be handled under the chosen
edition's terms.

## Safe next step

Before installing VMware Workstation Pro or VirtualBox on the host, the user
must approve that host-level change. Installation should be performed by an
administrator and may require a reboot. Do not change firmware virtualization,
Secure Boot, host driver signing, the host default audio device, or host audio
drivers.

After an approved hypervisor is installed and host RAM is available, provision
`SonicSync-Windows11-Test` on E: with 2 vCPUs, 4 GB RAM minimum (8 GB preferred
only if the host has sufficient free memory), an 80 GB dynamically allocated
disk, UEFI, virtual TPM 2.0, and virtual Secure Boot. Disable shared clipboard,
drag/drop, host physical audio passthrough, and shared folders. Use NAT only
for Windows/tool installation, then disconnect the VM network for audio
runtime tests. Create a clean snapshot before any guest-only test-signing or
driver install.

Driver test signing is not authorized by this assessment and has not been
configured. Before guest installation, review the driver package and removal
procedure, establish a restorable snapshot, explain any guest-only boot-policy
change, and get explicit approval for that guest change. Keep such changes
inside the disposable guest. Never relax the host's security policy.

## Official references

- [Windows 11 release information](https://learn.microsoft.com/en-us/windows/release-health/windows11-release-information) lists version 25H2 as OS build 26200.
- [Hyper-V system requirements](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/reference/hyper-v-requirements) lists supported editions and hardware prerequisites; [Microsoft installation guidance](https://learn.microsoft.com/en-us/windows-server/virtualization/hyper-v/get-started/install-hyper-v) states the full Hyper-V role is unavailable on Windows Home.
- [Windows 11 VM requirements](https://learn.microsoft.com/en-us/windows/whats-new/windows-11-requirements) specifies 2+ virtual processors, 4 GB+ memory, 64 GB+ storage, and Secure Boot/vTPM for Hyper-V VMs.
- [Microsoft Windows 11 x64 ISO download](https://www.microsoft.com/en-us/software-download/windows11) is the official multi-edition installation source.
- [Broadcom Workstation Pro download/licensing](https://knowledge.broadcom.com/external/article/368667/download-and-license-free-vmware-desktop.html) describes current free-use availability; [Windows 11 guest guidance](https://knowledge.broadcom.com/external/article/315650) documents guest TPM/Secure Boot requirements.

## Current validation state

This assessment performed no VM creation, Windows installation, source
transfer, signing configuration, driver installation, endpoint enumeration,
or external-application audio test. Prior SonicSync driver compilation and
static package validation are documented separately in
`docs/virtual-audio-endpoint-investigation.md`.
