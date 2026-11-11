param([string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$node = (Get-Command node.exe -ErrorAction Stop).Source
$repo = Split-Path -Parent $PSScriptRoot
if ($BuildDirectory) {
    if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $repo $BuildDirectory }
    $candidates = @($BuildDirectory)
} else {
    $candidates = @('build\windows-x64','build') | ForEach-Object { Join-Path $repo $_ }
}
$mediaExecutable = $candidates | ForEach-Object { Join-Path $_ 'Release\sonicsync_media_session_remote.exe' } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $mediaExecutable) { throw 'Build the Release media session helper or supply its -BuildDirectory.' }
$temporary = Join-Path $env:TEMP ('SonicSyncRemoteSmoke-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
$previousPassword = $env:SONICSYNC_PFX_PASSWORD
$certificate = $null
$rsa = $null
try {
    $rsa = New-Object Security.Cryptography.RSACryptoServiceProvider -ArgumentList 2048
    $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
        'CN=SonicSync temporary loopback test', $rsa,
        [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false, $false, 0, $true))
    $usage = [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature -bor [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyEncipherment
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new($usage, $true))
    $oids = [Security.Cryptography.OidCollection]::new()
    [void]$oids.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.1'))
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($oids, $true))
    $san = [Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
    $san.AddDnsName('localhost')
    $request.CertificateExtensions.Add($san.Build())
    $certificate = $request.CreateSelfSigned((Get-Date).AddMinutes(-5), (Get-Date).AddDays(1))
    $password = [Convert]::ToBase64String([guid]::NewGuid().ToByteArray())
    $securePassword = ConvertTo-SecureString $password -AsPlainText -Force
    $pfx = Join-Path $temporary 'temporary-test.pfx'
    $der = Join-Path $temporary 'temporary-test.cer'
    [IO.File]::WriteAllBytes($pfx, $certificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Pfx, $securePassword))
    [IO.File]::WriteAllBytes($der, $certificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    $env:SONICSYNC_PFX_PASSWORD = $password
    & $node (Join-Path $PSScriptRoot '..\tests\windows_remote_host_smoke.mjs') $pfx $der $mediaExecutable
    if ($LASTEXITCODE -ne 0) { throw "Remote host smoke test failed with exit code $LASTEXITCODE." }
}
finally {
    if ($null -eq $previousPassword) { Remove-Item Env:\SONICSYNC_PFX_PASSWORD -ErrorAction SilentlyContinue }
    else { $env:SONICSYNC_PFX_PASSWORD = $previousPassword }
    if ($certificate) { $certificate.Dispose() }
    if ($rsa) { $rsa.Dispose() }
    foreach ($file in @('temporary-test.pfx', 'temporary-test.cer')) {
        Remove-Item -LiteralPath (Join-Path $temporary $file) -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $temporary
}
