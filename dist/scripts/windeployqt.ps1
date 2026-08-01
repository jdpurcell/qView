param (
    $BuildNum = ""
)

$qtVersion = [version](qmake -query QT_VERSION)
Write-Host "Detected Qt version $qtVersion"

# Run windeployqt
$isCrossCompile = $env:buildArch -eq 'Arm64'
$winDeployQt = $isCrossCompile ? "$env:QT_HOST_PATH\bin\windeployqt" : "windeployqt"
$argQtPaths = $isCrossCompile ? "--qtpaths=$env:QT_ROOT_DIR\bin\qtpaths.bat" : $null
& $winDeployQt $argQtPaths --no-compiler-runtime --no-translations "bin\qView.exe"

if ($qtVersion -ge [version]'6.8.1') {
    # Copy font so windows11 style can work on Windows 10
    New-Item -ItemType Directory -Path "bin\fonts" -Force
    Copy-Item -Path "dist\win\fonts\Segoe Fluent Icons.ttf" -Destination "bin\fonts"
}

$imfDir = "bin\imageformats"
if ((Test-Path "$imfDir\kimg_tga.dll") -and (Test-Path "$imfDir\qtga.dll")) {
    # Prefer kimageformats TGA plugin which supports more formats
    Write-Output "Removing duplicate TGA plugin"
    Remove-Item "$imfDir\qtga.dll"
}
