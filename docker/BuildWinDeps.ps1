# BuildWinDeps.ps1
# Builds BambuStudio third-party dependencies inside a Windows container.
# Called by Dockerfile.windows during the image build.
#
# Mirrors the steps in .github/workflows/build_deps.yml for the windows-latest runner.

$ErrorActionPreference = 'Stop'

# Locate the Visual Studio Build Tools installation
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found at '$vswhere'. Ensure Visual Studio Build Tools are installed."
}

$vsPath = & $vswhere -latest -products * `
    -requires 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' `
    -property installationPath

if (-not $vsPath) {
    throw "No Visual Studio installation with VC++ tools found."
}

$devCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
Write-Host "Using VS installation: $vsPath"
Write-Host "VsDevCmd.bat: $devCmd"

# Prepare the build directories
$depsDir    = 'C:\BambuStudio\deps\build'
$depsDestDir = "$depsDir\BambuStudio_dep"
New-Item -ItemType Directory -Path $depsDestDir -Force | Out-Null

# Write the build commands to a temporary batch file.
# Running everything inside a single cmd.exe session ensures that the VS
# developer environment (set by VsDevCmd.bat) is available to cmake and msbuild.
$batchContent = @(
    "@echo off",
    "call `"$devCmd`" -arch=x64 -host_arch=x64",
    "if %errorlevel% neq 0 exit /b %errorlevel%",
    "cd /d `"$depsDir`"",
    "if %errorlevel% neq 0 exit /b %errorlevel%",
    "cmake .. -G `"Visual Studio 17 2022`" -A x64 -DDESTDIR=`".\BambuStudio_dep`" -DCMAKE_BUILD_TYPE=Release -DDEP_DEBUG=OFF",
    "if %errorlevel% neq 0 exit /b %errorlevel%",
    "msbuild /m ALL_BUILD.vcxproj /p:Configuration=Release",
    "if %errorlevel% neq 0 exit /b %errorlevel%"
)

$batchFile = [System.IO.Path]::Combine([System.IO.Path]::GetTempPath(), 'build_deps.bat')
[System.IO.File]::WriteAllLines($batchFile, $batchContent, [System.Text.Encoding]::ASCII)

Write-Host "Running dependency build..."
& cmd /c $batchFile
$exitCode = $LASTEXITCODE
Remove-Item $batchFile -Force -ErrorAction SilentlyContinue

if ($exitCode -ne 0) {
    throw "Dependency build failed with exit code: $exitCode"
}

Write-Host "Dependencies built successfully."
