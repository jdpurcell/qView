#!/usr/bin/env pwsh

using namespace System.Security.Cryptography.X509Certificates

if (-not $env:APPLE_DEVID_APP_CERT_DATA) {
    return
}

$certPath = Join-Path $env:RUNNER_TEMP 'codesign.p12'
$keychainPath = Join-Path $env:RUNNER_TEMP 'codesign.keychain-db'
$keychainPass = [Guid]::NewGuid().ToString()

[IO.File]::WriteAllBytes($certPath, [Convert]::FromBase64String($env:APPLE_DEVID_APP_CERT_DATA))

# Create a keychain
& security create-keychain -p $keychainPass $keychainPath
# Set it as default
& security default-keychain -s $keychainPath
# Unlock it
& security unlock-keychain -p $keychainPass
# Clear settings to prevent locking after timeout
& security set-keychain-settings
# Import certificate and allow codesign to access it
& security import $certPath -P $env:APPLE_DEVID_APP_CERT_PASS -T /usr/bin/codesign

$cert = New-Object X509Certificate2($certPath, $env:APPLE_DEVID_APP_CERT_PASS)
$certName = $cert.GetNameInfo([X509NameType]::SimpleName, $false)
[Environment]::SetEnvironmentVariable('APPLE_DEVID_APP_CERT_NAME', $certName)
