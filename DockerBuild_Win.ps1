# DockerBuild_Win.ps1
# Helper script to build BambuStudio in a Windows container.
# Mirrors DockerBuild.sh, which performs the same role for Linux containers.
#
# Requirements:
#   - Docker Desktop for Windows switched to Windows Containers mode
#   - Windows 10/11 with Hyper-V, or Windows Server 2019/2022
#
# Usage examples:
#   .\DockerBuild_Win.ps1                                    # Build image only
#   .\DockerBuild_Win.ps1 -ExtractArtifacts                 # Build and copy install-dir locally
#   .\DockerBuild_Win.ps1 -ExtractArtifacts -OutputPath C:\builds\BambuStudio

param(
    [switch]$ExtractArtifacts,
    [string]$OutputPath = ".\install-dir",
    [string]$ImageTag   = "bambustudio-build-win",
    [switch]$Help
)

if ($Help) {
    Write-Host @"
DockerBuild_Win.ps1 — Build BambuStudio inside a Windows container

Usage:
    .\DockerBuild_Win.ps1 [-ExtractArtifacts] [-OutputPath <path>] [-ImageTag <tag>]

Options:
    -ExtractArtifacts   Copy the compiled install-dir out of the container after the build.
    -OutputPath         Destination path for the extracted artifacts (default: .\install-dir).
    -ImageTag           Docker image name/tag to create (default: bambustudio-build-win).
    -Help               Show this help text.

Examples:
    .\DockerBuild_Win.ps1
    .\DockerBuild_Win.ps1 -ExtractArtifacts
    .\DockerBuild_Win.ps1 -ExtractArtifacts -OutputPath C:\builds\BambuStudio
"@
    exit 0
}

$ErrorActionPreference = 'Stop'
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

# ---------------------------------------------------------------------------
# Verify Docker is running in Windows Containers mode
# ---------------------------------------------------------------------------
try {
    $dockerInfo = docker info 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "docker info returned exit code $LASTEXITCODE"
    }
    if ($dockerInfo -notmatch 'OSType:\s*windows') {
        Write-Error @"
Docker is not running in Windows Containers mode.
Right-click the Docker Desktop system-tray icon and choose
'Switch to Windows containers...', then re-run this script.
"@
        exit 1
    }
} catch {
    Write-Error "Could not contact Docker. Is Docker Desktop running?`n$_"
    exit 1
}

# ---------------------------------------------------------------------------
# Build the Docker image
# ---------------------------------------------------------------------------
Write-Host "Building Windows container image '$ImageTag' ..."
Write-Host "Dockerfile : $ScriptRoot\Dockerfile.windows"
Write-Host "Build context : $ScriptRoot"
Write-Host ""

docker build `
    -f "$ScriptRoot\Dockerfile.windows" `
    -t $ImageTag `
    $ScriptRoot

if ($LASTEXITCODE -ne 0) {
    Write-Error "docker build failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "Image '$ImageTag' built successfully."

# ---------------------------------------------------------------------------
# Optionally extract build artifacts from the image
# ---------------------------------------------------------------------------
if ($ExtractArtifacts) {
    Write-Host ""
    Write-Host "Extracting artifacts to: $OutputPath"

    $ContainerName = "bbs-win-extract-$(Get-Random)"
    docker create --name $ContainerName $ImageTag | Out-Null

    try {
        docker cp "${ContainerName}:C:/BambuStudio/install-dir" $OutputPath
        if ($LASTEXITCODE -ne 0) {
            throw "docker cp failed with exit code $LASTEXITCODE"
        }
        Write-Host "Artifacts extracted to: $((Resolve-Path $OutputPath).Path)"
    } finally {
        docker rm $ContainerName | Out-Null
    }
}

Write-Host ""
Write-Host "Done."
