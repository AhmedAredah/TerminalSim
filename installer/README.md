# TerminalSim Installer

Cross-platform packaging for TerminalSim. Produces native packages (DEB, RPM,
NSIS, DragNDrop) via CPack and a polished GUI installer via Qt Installer
Framework.

## Layout

```
installer/
├── CMakeLists.txt    Subproject entry; gated by -DBUILD_INSTALLER=ON
├── cpack/            Generator-specific CPack configuration
├── ifw/              Qt Installer Framework templates and CMake driver
├── deploy/           Qt + RabbitMQ-C + Container runtime bundling helpers
├── service/          systemd / launchd / Windows Service unit files
└── assets/           Icons and branding (see assets/README.md)
```

## Quick start

Configure with the installer enabled:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_INSTALLER=ON
cmake --build build -j
```

Then produce packages:

```
# Build all packages for the host platform (auto-selects generators).
cd build && cpack
```

To enable the Qt Installer Framework generator, point CMake at your IFW
install with `-DCPACK_IFW_ROOT=...` or set `QTIFWDIR` in the environment.
Skip IFW with `-DTERMINALSIM_DISABLE_IFW=ON` if it is unavailable.

## Output artifacts

| Platform | Generators                  | Output                                                                  |
|----------|-----------------------------|-------------------------------------------------------------------------|
| Linux    | `IFW`, `DEB`, `RPM`, `TGZ`  | `TerminalSim-<ver>-Linux-<arch>.{run,deb,rpm,tar.gz}`                   |
| Windows  | `IFW`, `NSIS`, `ZIP`        | `TerminalSim-<ver>-Windows-AMD64.{exe,zip}` (IFW emits a separate `.exe`) |
| macOS    | `IFW`, `DragNDrop`, `TGZ`   | `TerminalSim-<ver>-Darwin-<arch>.{app,dmg,tar.gz}`                      |

## Components

The install tree is split into CPack components so installers can offer
granular selection:

* **Runtime** (required) - Server binary, Qt runtime, RabbitMQ-C, Container.
* **Service** (optional, default off) - systemd unit, launchd plist, Windows
  Service registration scripts. Files are placed; activation is always manual.

## Runtime bundling

`installer/deploy/` runs the platform-native deployment tools at install time:

* **Linux** - `linuxdeploy --plugin qt`, falling back to `patchelf` for RPATH
  if linuxdeploy is not on `PATH`.
* **Windows** - `windeployqt --release --compiler-runtime`.
* **macOS** - `macdeployqt`, then `install_name_tool` to relocate non-Qt
  dylibs under `@executable_path/../lib/`.

This makes the package self-contained: end users do not need to install Qt or
RabbitMQ-C separately.

## Service installation

The installer **does not** enable or start the service. Operators opt in:

```
# Linux
sudo systemctl enable --now terminalsim.service

# macOS
sudo launchctl load -w /Library/LaunchDaemons/com.aredah.terminalsim.plist

# Windows
.\share\terminalsim\service\windows\install-service.ps1 \
    -InstallDir "C:\Program Files\TerminalSim"
```

## Code signing and notarization

Optional, enabled via environment variables read at install time:

| Variable                          | Effect                                         |
|-----------------------------------|------------------------------------------------|
| `TERMINALSIM_SIGN_IDENTITY`       | macOS `codesign --options runtime` identity.   |
| `TERMINALSIM_NOTARIZE_PROFILE`    | macOS notarytool keychain profile (future).    |
| `TERMINALSIM_SIGN_CERT`           | Windows signtool certificate path (future).    |

Hooks are wired through `deploy_macos.cmake`. Signing and notarization
secrets are never committed.

## CI

`.github/workflows/release.yml` builds installer artifacts on tag push
(`v*`) for Linux, Windows, and macOS, and attaches them to the matching
GitHub Release.
