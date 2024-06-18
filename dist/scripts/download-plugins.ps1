#!/usr/bin/env pwsh

# This script will download binary plugins from the kimageformats-binaries repository using Github's API.

$pluginNames = "qtapng", "kimageformats"

$qtVersion = [version]((qmake --version -split '\n')[1] -split ' ')[3]
Write-Host "Detected Qt Version $qtVersion"

if ($IsWindows) {
    $imageName = "windows-2022"
} elseif ($IsMacOS) {
    $imageName = "macos-14"
} else {
    $imageName = "ubuntu-20.04"
}

$binaryBaseUrl = "https://github.com/jdpurcell/kimageformats-binaries/releases/download/cont"

if ($pluginNames.count -eq 0) {
    Write-Host "the pluginNames array is empty."
}

foreach ($pluginName in $pluginNames) {
    $pluginQtVersion = "6.5.3"
    $artifactName = "$pluginName-$imageName-$pluginQtVersion$($env:arch ? "-$env:arch" : $null).zip"
    $downloadUrl = "$binaryBaseUrl/$artifactName"

    Write-Host "Downloading $downloadUrl"
    Invoke-WebRequest -URI $downloadUrl -OutFile $artifactName
    Expand-Archive $artifactName -DestinationPath $pluginName
    Remove-Item $artifactName
}

if ($IsWindows) {
    $out_frm = "bin/"
    $out_imf = "bin/imageformats"
} elseif ($IsMacOS) {
    $out_frm = "bin/qView.app/Contents/Frameworks"
    $out_imf = "bin/qView.app/Contents/PlugIns/imageformats"
} else {
    $out_frm = "bin/appdir/usr/lib"
    $out_imf = "bin/appdir/usr/plugins/imageformats"
}

New-Item -Type Directory -Path "$out_frm" -ErrorAction SilentlyContinue
New-Item -Type Directory -Path "$out_imf" -ErrorAction SilentlyContinue

# Copy QtApng
if ($pluginNames -contains 'qtapng') {
    if ($IsWindows) {
        cp qtapng/QtApng/output/qapng.dll "$out_imf/"
    } elseif ($IsMacOS) {
        cp qtapng/QtApng/output/libqapng.* "$out_imf/"
    } else {
        cp qtapng/QtApng/output/libqapng.so "$out_imf/"
    }
}

function CopyFrameworkDlls($mainDll, $otherDlls) {
    if (-not (Test-Path -Path kimageformats/kimageformats/output/$mainDll -PathType Leaf)) {
        return
    }
    foreach ($dll in @($mainDll) + $otherDlls) {
        cp kimageformats/kimageformats/output/$dll "$out_frm/"
    }
}

if ($pluginNames -contains 'kimageformats') {
    $kfMajorVer = $qtVersion -ge [version]'6.5.0' ? 6 : 5
    if ($IsWindows) {
        mv kimageformats/kimageformats/output/kimg_*.dll "$out_imf/"
        CopyFrameworkDlls "KF$($kfMajorVer)Archive.dll" @("zlib1.dll")
        CopyFrameworkDlls "avif.dll" @("aom.dll")
        CopyFrameworkDlls "heif.dll" @("aom.dll", "libde265.dll", "libx265.dll")
        CopyFrameworkDlls "raw.dll" @("lcms2.dll", "zlib1.dll")
        CopyFrameworkDlls "jxl.dll" @("brotlicommon.dll", "brotlidec.dll", "brotlienc.dll", "hwy.dll", "jxl_cms.dll", "jxl_threads.dll", "lcms2.dll")
        CopyFrameworkDlls "OpenEXR-3_2.dll" @("deflate.dll", "Iex-3_2.dll", "IlmThread-3_2.dll", "Imath-3_1.dll", "OpenEXRCore-3_2.dll")
    } elseif ($IsMacOS) {
        cp kimageformats/kimageformats/output/kimg_*.* "$out_imf/"
        cp kimageformats/kimageformats/output/libKF?Archive.?.dylib "$out_frm/"
    } else {
        cp kimageformats/kimageformats/output/kimg_*.so "$out_imf/"
        cp kimageformats/kimageformats/output/libKF?Archive.so.? "$out_frm/"
    }
}
