# BuildWinApp.ps1
# Builds the BambuStudio application inside a Windows container.
# Called by Dockerfile.windows during the image build, after BuildWinDeps.ps1.
#
# Mirrors the steps in .github/workflows/build_bambu.yml for the windows-latest runner.

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

# Paths used by the build
$sourceDir   = 'C:\BambuStudio'
$buildDir    = 'C:\BambuStudio\build'
$installDir  = 'C:\BambuStudio\install-dir'
$depsPrefix  = 'C:\BambuStudio\deps\build\BambuStudio_dep\usr\local'

# The Windows 10 SDK path expected by the CMake configuration.
# SDK 22000 is installed by Dockerfile.windows via the VS Build Tools installer.
$win10SdkPath = 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.22000.0'

New-Item -ItemType Directory -Path $buildDir -Force | Out-Null

# Write the build commands to a temporary batch file so that cmake --build runs
# inside the same cmd.exe session that has the VS developer environment active.
$batchContent = @(
    "@echo off",
    "call `"$devCmd`" -arch=x64 -host_arch=x64",
    "if %errorlevel% neq 0 exit /b %errorlevel%",
    "cd /d `"$buildDir`"",
    "if %errorlevel% neq 0 exit /b %errorlevel%",
    "cmake `"$sourceDir`" -G `"Visual Studio 17 2022`" -A X64 ``",
    "    -DBBL_RELEASE_TO_PUBLIC=1 ``",
    "    -DBBL_INTERNAL_TESTING=0 ``",
    "    -DCMAKE_PREFIX_PATH=`"$depsPrefix`" ``",
    "    -DCMAKE_INSTALL_PREFIX=`"$installDir`" ``",
    "    -DCMAKE_BUILD_TYPE=Release ``",
    "    -DWIN10SDK_PATH=`"$win10SdkPath`"",
    "if %errorlevel% neq 0 exit /b %errorlevel%",
    "cmake --build . --target install --config Release -- -m",
    "if %errorlevel% neq 0 exit /b %errorlevel%"
)

$batchFile = [System.IO.Path]::Combine([System.IO.Path]::GetTempPath(), 'build_app.bat')
[System.IO.File]::WriteAllLines($batchFile, $batchContent, [System.Text.Encoding]::ASCII)

Write-Host "Running application build..."
& cmd /c $batchFile
$exitCode = $LASTEXITCODE
Remove-Item $batchFile -Force -ErrorAction SilentlyContinue

if ($exitCode -ne 0) {
    throw "Application build failed with exit code: $exitCode"
}

Write-Host "BambuStudio built successfully."
Write-Host "Artifacts are located at: $installDir"
