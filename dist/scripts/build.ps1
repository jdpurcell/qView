#!/usr/bin/env pwsh

param
(
    [switch]$CI,
    $Prefix = "/usr"
)

$qtVersion = [version](qmake -query QT_VERSION)
Write-Host "Detected Qt version $qtVersion"

if ($IsWindows -and $env:buildArch -eq 'Arm64') {
    $qtBinDir = Join-Path -Path $env:QT_ROOT_DIR -ChildPath 'bin'
    foreach ($name in 'qmake', 'qmake6', 'qtpaths', 'qtpaths6') {
        $targetExe = Join-Path -Path $qtBinDir -ChildPath "target-$name.exe"
        $exe = Join-Path -Path $qtBinDir -ChildPath "$name.exe"
        $bat = Join-Path -Path $qtBinDir -ChildPath "$name.bat"
        Move-Item -Path $targetExe -Destination $exe
        Remove-Item -Path $bat
    }

    $targetQtConf = Join-Path -Path $qtBinDir -ChildPath 'target_qt.conf'
    if (Test-Path $targetQtConf) {
        $targetQtConfContent = Get-Content -Path $targetQtConf -Raw
        $updatedTargetQtConfContent = $targetQtConfContent -replace '(?m)^HostSpec=win32-g\+\+$', 'HostSpec=win32-msvc'
        if ($updatedTargetQtConfContent -ne $targetQtConfContent) {
            Write-Host "Updating $targetQtConf to replace HostSpec=win32-g++ with HostSpec=win32-msvc"
            Set-Content -Path $targetQtConf -Value $updatedTargetQtConfContent -NoNewline
        } else {
            Write-Host "No HostSpec=win32-g++ line found in $targetQtConf; no update needed"
        }
    }
}

if ($IsWindows) {
    dist/scripts/vcvars.ps1
} elseif ($IsMacOS) {
    # By default CMake sets the deployment target for macOS to the build machine's version; set
    # it explicitly to the version supported by Qt for compatibility with older macOS versions
    $env:MACOSX_DEPLOYMENT_TARGET = (Select-String -Path (Join-Path $env:QT_ROOT_DIR 'mkspecs/qconfig.pri'), (Join-Path $env:QT_ROOT_DIR 'mkspecs/common/macx.conf') -Pattern '^\s*QMAKE_MACOSX_DEPLOYMENT_TARGET\s*=\s*(.+?)\s*$').Matches[0].Groups[1].Value
}

# Prepare CMake arguments
$cmakeArgs = @(
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_INSTALL_PREFIX=$Prefix"
)

if ($env:nightlyDefines) {
    $cmakeArgs += "-D$($env:nightlyDefines)"
}

if ($IsMacOS) {
    if ($env:buildArch -eq 'Universal') {
        $cmakeArgs += "-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64"
    } elseif ($env:buildArch -eq 'Arm64') {
        $cmakeArgs += "-DCMAKE_OSX_ARCHITECTURES=arm64"
    } elseif ($env:buildArch -eq 'X64') {
        $cmakeArgs += "-DCMAKE_OSX_ARCHITECTURES=x86_64"
    }
} elseif ($IsWindows) {
    # Workaround for https://developercommunity.visualstudio.com/t/10664660
    $cmakeArgs += '-DCMAKE_CXX_FLAGS=-D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR'
    if ($env:buildArch -eq 'X86') {
        $cmakeArgs += "-A Win32"
    } elseif ($env:buildArch -eq 'Arm64') {
        $cmakeArgs += "-A ARM64"
    }
}

# Create a build directory, configure, and build
New-Item -ItemType Directory -Force -Path build
Push-Location build
try {
    cmake $cmakeArgs ..
    cmake --build . --config Release --parallel
} finally {
    Pop-Location
}

# Copy artifact to bin directory for deployment scripts
New-Item -ItemType Directory -Force -Path bin
if ($IsWindows) {
    # MSVC generator might put executables in a config-specific subdirectory
    $exePath = "build/Release/qView.exe"
    if (-not (Test-Path $exePath)) {
        $exePath = "build/qView.exe"
    }
    Copy-Item -Path $exePath -Destination "bin/qView.exe" -Force
} elseif ($IsMacOS) {
    Copy-Item -Path "build/qView.app" -Destination "bin/qView.app" -Recurse -Force
} else {
    Copy-Item -Path "build/qview" -Destination "bin/qview" -Force
}
