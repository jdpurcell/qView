#!/usr/bin/env pwsh

param (
    $Prefix = "/usr"
)

$qtVersion = [version]((qmake --version -split '\n')[1] -split ' ')[3]
Write-Host "Detected Qt Version $qtVersion"

if ($IsWindows) {
    dist/scripts/vcvars.ps1
} elseif ($IsMacOS) {
    if ($qtVersion -lt [version]'6.5.3') {
        # Workaround for QTBUG-117484
        sudo xcode-select --switch /Applications/Xcode_14.3.1.app
    }
}

if ($IsMacOS) {
    $argDeviceArchs =
        $env:buildArch -eq 'X64' ? 'QMAKE_APPLE_DEVICE_ARCHS=x86_64' :
        $env:buildArch -eq 'Arm64' ? 'QMAKE_APPLE_DEVICE_ARCHS=arm64' :
        $env:buildArch -eq 'Universal' ? 'QMAKE_APPLE_DEVICE_ARCHS=x86_64 arm64' :
        $null
} elseif ($IsWindows) {
    # Workaround for https://developercommunity.visualstudio.com/t/10664660
    $argVcrMutexWorkaround = 'DEFINES+=_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR'
}
qmake PREFIX="$Prefix" DEFINES+="$env:nightlyDefines" $argVcrMutexWorkaround $argDeviceArchs

if ($IsWindows) {
    nmake
} else {
    make
}
