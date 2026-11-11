param(
    [string]$BuildDirectory = 'build\windows-x64',
    [string]$OutputDirectory = 'artifacts\windows'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
$out = if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $root $OutputDirectory }
$out = [IO.Path]::GetFullPath($out)
$stage = Join-Path $out 'stage'
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$ctest = Get-Command ctest -ErrorAction SilentlyContinue
if (-not $cmake -or -not $ctest) { throw 'CMake and CTest must be available on PATH. Use Developer PowerShell for VS 2022.' }
Push-Location $root
try {
    $cache = Join-Path $build 'CMakeCache.txt'
    $configureArgs = @('-A', 'x64')
    if ((Test-Path -LiteralPath $cache) -and (Select-String -LiteralPath $cache -Pattern '^CMAKE_GENERATOR:INTERNAL=Ninja$' -Quiet)) {
        $compiler = Get-ChildItem -Path (Join-Path $build 'CMakeFiles\*\CMakeCXXCompiler.cmake') |
            Where-Object { Select-String -LiteralPath $_.FullName -Pattern 'CMAKE_CXX_COMPILER_ARCHITECTURE_ID "?x64"?\)' -Quiet }
        if (-not $compiler) { throw 'This package requires an x64 build directory.' }
        $configureArgs = @('-DCMAKE_BUILD_TYPE=Release')
    }
    & $cmake.Source -S . -B $build @configureArgs
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & $cmake.Source --build $build --config Release --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
    & $ctest.Source --test-dir $build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed; refusing to package.' }
    if (Test-Path -LiteralPath $stage) {
        $resolvedStage = (Resolve-Path -LiteralPath $stage).ProviderPath
        if ([IO.Path]::GetDirectoryName($resolvedStage) -ne $out.TrimEnd('\') -or
            ((Get-Item -LiteralPath $stage).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Refusing to remove a staging path outside the output directory.'
        }
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $stage 'payload') | Out-Null
    & $cmake.Source --install $build --config Release --prefix (Join-Path $stage 'payload')
    if ($LASTEXITCODE -ne 0) { throw 'CMake install staging failed.' }
    Copy-Item (Join-Path $PSScriptRoot 'Install-SonicSync.ps1') $stage
    Copy-Item (Join-Path $PSScriptRoot 'Uninstall-SonicSync.ps1') $stage
    Copy-Item (Join-Path $PSScriptRoot 'INSTALL.txt') $stage
    $remoteStage = Join-Path $stage 'RemoteHost'
    New-Item -ItemType Directory -Force -Path $remoteStage | Out-Null
    Copy-Item (Join-Path $root 'tools\Start-SonicSyncRemoteHost.ps1') $remoteStage
    Copy-Item (Join-Path $root 'tools\windows_remote_host.mjs') $remoteStage
    Copy-Item (Join-Path $root 'docs\windows-remote-control.md') $remoteStage
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $archive = Join-Path $out 'SonicSync-1.0.2-win-x64.zip'
    $temporaryArchive = Join-Path $out ('.SonicSync-' + [guid]::NewGuid().ToString('N') + '.zip')
    try {
        Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $temporaryArchive -CompressionLevel Optimal
        Move-Item -LiteralPath $temporaryArchive -Destination $archive -Force
    } finally {
        if (Test-Path -LiteralPath $temporaryArchive) { Remove-Item -LiteralPath $temporaryArchive }
    }
    Write-Host "Release bundle created: $archive"
} finally { Pop-Location }
