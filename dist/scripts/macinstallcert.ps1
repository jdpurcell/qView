#!/usr/bin/env pwsh

using namespace System.Security.Cryptography.X509Certificates

if ($env:APPLE_DEVID_APP_CERT_DATA) {
    $certPath = Join-Path $env:RUNNER_TEMP 'codesign.p12'
    $keychainPath = Join-Path $env:RUNNER_TEMP 'codesign.keychain-db'
    $keychainPass = [Guid]::NewGuid().ToString()

    [IO.File]::WriteAllBytes($certPath, [Convert]::FromBase64String($env:APPLE_DEVID_APP_CERT_DATA))

    & security create-keychain -p $keychainPass $keychainPath
    & security unlock-keychain -p $keychainPass $keychainPath
    & security import $certPath -P $env:APPLE_DEVID_APP_CERT_PASS -A -t cert -f pkcs12 -k $keychainPath
    & security set-key-partition-list -S apple-tool:,apple: -s -k $keychainPass $keychainPath
    & security list-keychains -d user -s $keychainPath

    $cert = New-Object X509Certificate2($certPath, $env:APPLE_DEVID_APP_CERT_PASS)
    $certName = $cert.GetNameInfo([X509NameType]::SimpleName, $false)
}
else {
    $certName = '-'
}

[Environment]::SetEnvironmentVariable('CODESIGN_CERT_NAME', $certName)
