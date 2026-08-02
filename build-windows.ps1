$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$source = Join-Path $root "Windows\AMDownload\AMDownload.cpp"
$resource = Join-Path $root "Windows\AMDownload\AMDownload.rc"
$build = Join-Path $root "build\windows"
$resourceObject = Join-Path $build "AMDownload.res"
$executable = Join-Path $build "AMDownload.exe"
$installerSource = Join-Path $root "Windows\Installer\AMDownloadSetup.cpp"
$installerResource = Join-Path $root "Windows\Installer\AMDownloadSetup.rc"
$installerBuild = Join-Path $root "build\installer"
$installerResourceObject = Join-Path $installerBuild "AMDownloadSetup.res"
$installerObject = Join-Path $installerBuild "AMDownloadSetup.obj"
$installerExecutable = Join-Path $installerBuild "AMDownloadSetup.exe"

New-Item -ItemType Directory -Force -Path $build | Out-Null
New-Item -ItemType Directory -Force -Path $installerBuild | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio was not found. Install the Desktop development with C++ workload."
}

$vsInstall = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if (-not $vsInstall) {
    throw "The Visual C++ x64 toolchain was not found. Install the Desktop development with C++ workload."
}

$vcVars = Join-Path $vsInstall "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path -LiteralPath $vcVars)) {
    throw "The Visual C++ environment script was not found at $vcVars"
}

$object = Join-Path $build "AMDownload.obj"
$command = 'call "' + $vcVars + '" && rc.exe /nologo /fo"' + $resourceObject + '" "' + $resource + '" && cl.exe /nologo /std:c++20 /EHsc /W4 /O2 /Fo"' + $object + '" /Fe"' + $executable + '" "' + $source + '" "' + $resourceObject + '" /link /SUBSYSTEM:WINDOWS'
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw "The Windows build failed with exit code $LASTEXITCODE."
}

Copy-Item -LiteralPath $executable -Destination (Join-Path $installerBuild "AMDownload.exe") -Force
$toolPayload = Join-Path $build "tools"
$toolDestination = Join-Path $installerBuild "tools"
if (Test-Path -LiteralPath $toolPayload) {
    Copy-Item -LiteralPath $toolPayload -Destination $toolDestination -Recurse -Force
}

$installerCommand = 'call "' + $vcVars + '" && rc.exe /nologo /fo"' + $installerResourceObject + '" "' + $installerResource + '" && cl.exe /nologo /std:c++20 /EHsc /W4 /O2 /Fo"' + $installerObject + '" /Fe"' + $installerExecutable + '" "' + $installerSource + '" "' + $installerResourceObject + '" /link /SUBSYSTEM:WINDOWS'
& cmd.exe /d /s /c $installerCommand
if ($LASTEXITCODE -ne 0) {
    throw "The installer build failed with exit code $LASTEXITCODE."
}

Write-Host "Built app: $executable"
Write-Host "Built installer: $installerExecutable"
