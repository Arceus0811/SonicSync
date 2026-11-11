param(
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'Programs\SonicSync'),
    [string]$DesktopDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::DesktopDirectory),
    [string]$StartMenuDirectory = (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\SonicSync'),
    [string]$RegistryKeyPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SonicSync',
    [switch]$NoRegistry
)
$ErrorActionPreference = 'Stop'
$payload = Join-Path $PSScriptRoot 'payload'
$required = @('sonicsync_gui.exe','sonicsync_player.exe','sonicsync_wasapi.exe','sonicsync_virtual_endpoint_relay.exe','sonicsync_media_session_remote.exe')
foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $payload $file) -PathType Leaf)) { throw "Release payload is missing $file" }
}

$remoteSource = Join-Path $PSScriptRoot 'RemoteHost'
$remoteFiles = @('Start-SonicSyncRemoteHost.ps1','windows_remote_host.mjs','windows-remote-control.md')
foreach ($file in $remoteFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $remoteSource $file) -PathType Leaf)) { throw "Remote host payload is missing $file" }
}
$remoteDirectory = Join-Path $InstallDirectory 'RemoteHost'
New-Item -ItemType Directory -Force -Path $InstallDirectory,$DesktopDirectory,$StartMenuDirectory,$remoteDirectory | Out-Null
foreach ($file in $required) { Copy-Item -LiteralPath (Join-Path $payload $file) -Destination (Join-Path $InstallDirectory $file) -Force }
foreach ($file in $remoteFiles) { Copy-Item -LiteralPath (Join-Path $remoteSource $file) -Destination (Join-Path $remoteDirectory $file) -Force }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Uninstall-SonicSync.ps1') -Destination (Join-Path $InstallDirectory 'Uninstall-SonicSync.ps1') -Force

$shell = New-Object -ComObject WScript.Shell
$exe = Join-Path $InstallDirectory 'sonicsync_gui.exe'
foreach ($shortcutPath in @((Join-Path $DesktopDirectory 'SonicSync.lnk'),(Join-Path $StartMenuDirectory 'SonicSync.lnk'))) {
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $exe
    $shortcut.WorkingDirectory = $InstallDirectory
    $shortcut.IconLocation = "$exe,0"
    $shortcut.Description = 'SonicSync personal audio'
    $shortcut.Save()
}

if (-not $NoRegistry) {
    New-Item -Path $RegistryKeyPath -Force | Out-Null
    $uninstaller = Join-Path $InstallDirectory 'Uninstall-SonicSync.ps1'
    $uninstallCommand = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "{0}" -InstallDirectory "{1}" -DesktopDirectory "{2}" -StartMenuDirectory "{3}" -RegistryKeyPath "{4}"' -f $uninstaller,$InstallDirectory,$DesktopDirectory,$StartMenuDirectory,$RegistryKeyPath
    Set-ItemProperty -Path $RegistryKeyPath -Name DisplayName -Value 'SonicSync'
    Set-ItemProperty -Path $RegistryKeyPath -Name DisplayVersion -Value '1.0.2'
    Set-ItemProperty -Path $RegistryKeyPath -Name Publisher -Value 'SonicSync Audio'
    Set-ItemProperty -Path $RegistryKeyPath -Name DisplayIcon -Value "$exe,0"
    Set-ItemProperty -Path $RegistryKeyPath -Name UninstallString -Value $uninstallCommand
    Set-ItemProperty -Path $RegistryKeyPath -Name InstallLocation -Value $InstallDirectory
    Set-ItemProperty -Path $RegistryKeyPath -Name InstallDate -Value (Get-Date -Format 'yyyyMMdd')
    Set-ItemProperty -Path $RegistryKeyPath -Name NoModify -Type DWord -Value 1
    Set-ItemProperty -Path $RegistryKeyPath -Name NoRepair -Type DWord -Value 1
}
Write-Host "SonicSync 1.0.2 installed to $InstallDirectory"
Write-Host "Start Menu shortcut: $(Join-Path $StartMenuDirectory 'SonicSync.lnk')"
Write-Host "Desktop shortcut: $(Join-Path $DesktopDirectory 'SonicSync.lnk')"
