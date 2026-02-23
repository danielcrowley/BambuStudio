# Building BambuStudio in a Windows Container

This guide explains how to compile BambuStudio inside a Docker **Windows container** running on a Windows host. This is useful for reproducible builds, CI pipelines, or keeping the build environment isolated from your host machine.

## Requirements

| Requirement | Notes |
|---|---|
| **Windows 10 or 11** (64-bit) | Pro / Enterprise / Education with Hyper-V enabled, *or* Windows Server 2019/2022 |
| **Docker Desktop for Windows** | Must be switched to **Windows Containers** mode (see below) |
| **~50 GB free disk space** | The base image + VS Build Tools + source + compiled output are large |
| **~16 GB RAM** | Recommended for the parallel build |

> **Hyper-V isolation vs. process isolation**
> Windows container process isolation requires the container OS version to *exactly* match the host OS build number. Hyper-V isolation (the default in Docker Desktop) works across different OS versions. The `Dockerfile.windows` targets `mcr.microsoft.com/windows/servercore:ltsc2022`; if your host is not Server 2022 / Windows 11 22H2+, Hyper-V isolation is used automatically.

## Switching Docker Desktop to Windows Containers mode

1. Right-click the Docker Desktop icon in the system tray.
2. Select **"Switch to Windows containers..."** and confirm.
3. Wait for Docker to restart.

You can verify the mode with:
```powershell
docker info | Select-String OSType
# Expected output:  OSType: windows
```

## Quick start — using the helper script

From a PowerShell terminal in the repository root:

```powershell
# Build the image (takes a long time on first run — VS Build Tools + all deps)
.\DockerBuild_Win.ps1

# Build the image and copy the compiled artifacts to .\install-dir
.\DockerBuild_Win.ps1 -ExtractArtifacts

# Build the image, then extract to a custom path
.\DockerBuild_Win.ps1 -ExtractArtifacts -OutputPath C:\builds\BambuStudio
```

The script checks that Docker is in Windows Containers mode before starting and provides a clear error message if it is not.

## Manual build steps

If you prefer to drive Docker directly:

### 1. Build the image

```powershell
docker build -t bambustudio-build-win -f Dockerfile.windows .
```

The Dockerfile performs the following steps automatically:

1. Installs **Chocolatey**, then Git, CMake, Strawberry Perl, and pkg-config.
2. Downloads and installs **Visual Studio 2022 Build Tools** with the MSVC C++ toolchain and the Windows 10 SDK (22000).
3. Copies the source tree into the container.
4. Builds third-party **dependencies** (`deps/build/BambuStudio_dep`) via `docker\BuildWinDeps.ps1`.
5. Builds the **BambuStudio application** (`install-dir`) via `docker\BuildWinApp.ps1`.

### 2. Extract the compiled binaries

```powershell
docker create --name bbs-extract bambustudio-build-win
docker cp bbs-extract:C:/BambuStudio/install-dir ./install-dir
docker rm bbs-extract
```

The extracted `install-dir` folder contains the portable BambuStudio build — the same layout produced by the `build_win.bat` script on a bare-metal machine.

### 3. (Optional) Open an interactive shell in the container

```powershell
docker run -it --rm bambustudio-build-win powershell
```

## Speeding up iterative builds

Windows container builds are slow on first run because VS Build Tools must be downloaded and installed. To avoid rebuilding from scratch every time:

- **Use Docker layer caching** — Docker caches each `RUN` layer. Changing only the source code (after the `COPY . .` instruction) will not re-run the VS installation or dependency build layers, provided you have not cleaned the build cache.
- **Separate deps and app builds** — The Dockerfile places the dependency build (`BuildWinDeps.ps1`) in a layer before the application build (`BuildWinApp.ps1`). Modifying only application source files will reuse the cached deps layer.
- **Avoid `docker build --no-cache`** — Only use `--no-cache` when you need to pick up updated dependencies or toolchain changes.

## What is built

| Path inside container | Contents |
|---|---|
| `C:\BambuStudio\deps\build\BambuStudio_dep` | Compiled third-party libraries |
| `C:\BambuStudio\build` | CMake build tree for the application |
| `C:\BambuStudio\install-dir` | **Final artifacts** — copy this out of the container |

## Troubleshooting

### "Docker is not running in Windows Containers mode"
Switch Docker Desktop to Windows Containers mode as described above.

### VS Build Tools installation exits with a non-zero code
The installer sometimes exits with `1602` (user cancelled) or `1603` (fatal error) inside containers due to missing system components. Ensure your Docker Desktop has enough resources assigned (CPU, RAM) in **Settings → Resources**.

### cmake cannot find the Windows SDK
The SDK is expected at `C:\Program Files (x86)\Windows Kits\10\Include\10.0.22000.0`. If the VS Build Tools installer did not install it, open the `Dockerfile.windows` and verify that `Microsoft.VisualStudio.Component.Windows10SDK.22000` is included in the component list.

### Build runs out of disk space
Increase the Docker VM disk size in **Docker Desktop → Settings → Resources → Virtual disk limit** and ensure you have at least 50 GB free on the host drive that stores Docker images.
