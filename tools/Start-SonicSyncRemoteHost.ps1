param(
    [Parameter(Mandatory = $true)][string]$ListenAddress,
    [ValidateRange(1024, 65535)][int]$Port = 47931,
    [string]$BuildDirectory
)
$ErrorActionPreference = 'Stop'

$address = [System.Net.IPAddress]::Parse($ListenAddress)
$localAddresses = [Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces() |
    ForEach-Object { $_.GetIPProperties().UnicastAddresses } | ForEach-Object { $_.Address }
if ($address -notin $localAddresses) {
    throw "Listen address $ListenAddress is not assigned to this PC. Pick one from Get-NetIPAddress."
}
$repo = Split-Path -Parent $PSScriptRoot
$name = 'sonicsync_media_session_remote.exe'
if ($BuildDirectory) {
    if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $repo $BuildDirectory }
    $candidates = @(Join-Path $BuildDirectory "Release\$name")
} else {
    $candidates = @($name, "payload\$name", "build\windows-x64\Release\$name", "build\Release\$name") |
        ForEach-Object { Join-Path $repo $_ }
}
$remoteExe = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $remoteExe) { throw "Missing $name. Reinstall the complete package or build the Release target and supply -BuildDirectory." }
$remoteScript = Join-Path $PSScriptRoot 'windows_remote_host.mjs'
if (-not (Test-Path -LiteralPath $remoteScript -PathType Leaf)) { throw "Remote host script missing: $remoteScript" }
$node = (Get-Command node.exe -ErrorAction Stop).Source
$root = Join-Path $env:LOCALAPPDATA 'SonicSync\RemoteHost'
New-Item -ItemType Directory -Force -Path $root | Out-Null
$pfxPath = Join-Path $root 'remote-host.pfx'
$derPath = Join-Path $root 'remote-host.cer'
$passwordPath = Join-Path $root 'pfx-password.dpapi'

if (-not (Test-Path -LiteralPath $pfxPath) -or -not (Test-Path -LiteralPath $derPath) -or -not (Test-Path -LiteralPath $passwordPath)) {
    $randomBytes = New-Object byte[] 32
    $rng = New-Object Security.Cryptography.RNGCryptoServiceProvider
    $rng.GetBytes($randomBytes)
    $rng.Dispose()
    $plainPassword = [Convert]::ToBase64String($randomBytes)
    $securePassword = ConvertTo-SecureString -String $plainPassword -AsPlainText -Force
    $rsa = New-Object Security.Cryptography.RSACryptoServiceProvider -ArgumentList 2048
    $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
        "CN=SonicSync Remote $env:COMPUTERNAME", $rsa,
        [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false, $false, 0, $true))
    $usage = [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature -bor [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyEncipherment
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new($usage, $true))
    $oids = [Security.Cryptography.OidCollection]::new()
    [void]$oids.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.1'))
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($oids, $true))
    $san = [Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
    $san.AddDnsName($env:COMPUTERNAME)
    $request.CertificateExtensions.Add($san.Build())
    $certificate = $request.CreateSelfSigned((Get-Date).AddMinutes(-5), (Get-Date).AddYears(2))
    [IO.File]::WriteAllBytes($pfxPath, $certificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Pfx, $securePassword))
    [IO.File]::WriteAllBytes($derPath, $certificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    $certificate.Dispose()
    $rsa.Dispose()
    $securePassword | ConvertFrom-SecureString | Set-Content -LiteralPath $passwordPath -Encoding ascii
}

$encryptedPassword = Get-Content -LiteralPath $passwordPath -Raw | ConvertTo-SecureString
$previousPassword = $env:SONICSYNC_PFX_PASSWORD
try {
    $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($encryptedPassword)
    try { $env:SONICSYNC_PFX_PASSWORD = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer) }
    & $node $remoteScript --listen-address $address.IPAddressToString --port $Port `
        --pfx $pfxPath --certificate $derPath --media-session-exe $remoteExe
    if ($LASTEXITCODE -ne 0) { throw "Remote host exited with code $LASTEXITCODE." }
}
finally {
    if ($null -eq $previousPassword) { Remove-Item Env:\SONICSYNC_PFX_PASSWORD -ErrorAction SilentlyContinue }
    else { $env:SONICSYNC_PFX_PASSWORD = $previousPassword }
}
