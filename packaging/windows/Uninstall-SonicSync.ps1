param(
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'Programs\SonicSync'),
    [string]$DesktopDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::DesktopDirectory),
    [string]$StartMenuDirectory = (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\SonicSync'),
    [string]$RegistryKeyPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\SonicSync',
    [switch]$NoRegistry
)
$ErrorActionPreference = 'Stop'
foreach ($process in (Get-Process sonicsync_gui -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $InstallDirectory 'sonicsync_gui.exe') })) {
    if (-not $process.CloseMainWindow() -or -not $process.WaitForExit(10000)) {
        throw 'Close the SonicSync studio so it can save your library, then run the uninstaller again.'
    }
}
$shell = New-Object -ComObject WScript.Shell
foreach ($path in @((Join-Path $DesktopDirectory 'SonicSync.lnk'),(Join-Path $StartMenuDirectory 'SonicSync.lnk'))) {
    if ((Test-Path -LiteralPath $path -PathType Leaf) -and
        $shell.CreateShortcut($path).TargetPath -eq (Join-Path $InstallDirectory 'sonicsync_gui.exe')) {
        Remove-Item -LiteralPath $path -Force
    }
}
$programFiles = @('sonicsync_gui.exe','sonicsync_player.exe','sonicsync_wasapi.exe','sonicsync_virtual_endpoint_relay.exe','sonicsync_media_session_remote.exe',
    'RemoteHost\Start-SonicSyncRemoteHost.ps1','RemoteHost\windows_remote_host.mjs','RemoteHost\windows-remote-control.md')
foreach ($file in $programFiles) {
    $path = Join-Path $InstallDirectory $file
    if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force }
}
if (-not $NoRegistry -and (Test-Path -LiteralPath $RegistryKeyPath)) {
    $entry = Get-ItemProperty -LiteralPath $RegistryKeyPath
    if ($entry.DisplayName -eq 'SonicSync' -and $entry.InstallLocation -eq $InstallDirectory) {
        Remove-Item -LiteralPath $RegistryKeyPath -Force
    }
}
$scriptPath = Join-Path $InstallDirectory 'Uninstall-SonicSync.ps1'
if (Test-Path -LiteralPath $scriptPath -PathType Leaf) { Remove-Item -LiteralPath $scriptPath -Force }
foreach ($directory in @((Join-Path $InstallDirectory 'RemoteHost'), $StartMenuDirectory, $InstallDirectory)) {
    if ((Test-Path -LiteralPath $directory -PathType Container) -and
        -not (Get-ChildItem -LiteralPath $directory -Force | Select-Object -First 1)) {
        Remove-Item -LiteralPath $directory -Force
    }
}
Write-Host 'SonicSync program files and shortcuts were removed. User data was left untouched.'
