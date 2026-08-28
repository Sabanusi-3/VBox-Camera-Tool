# VBox Camera Tool

> **ALPHA software.** This project is currently intended for developers and technical testers. Expect rough setup, breaking changes, and environment-specific bugs.

VBox Camera Tool captures a VirtualBox VM and exposes it to OBS Studio as a dedicated **VBox Camera** source.

The goal is simple: capture the VM itself instead of capturing a host window or desktop. That makes VM recording/streaming safer and cleaner, especially when unrelated host windows, notifications, or audio must not leak into the recording.

## Current features

- VirtualBox VM video capture through the VirtualBox API
- OBS Studio source named **VBox Camera**
- Shared-memory video transport between the Bridge and OBS plugin
- 4:3 and 16:9 output modes
- Adjustable 1-60 FPS output
- VM selection by name in OBS, stored internally by UUID
- Automatic reconnect when the selected VM stops and starts again
- OBS Application Audio Capture integration for VirtualBox audio
- Process-specific audio capture so unrelated host audio is not mixed into VBox Camera
- Automatic audio child-source reinitialization after VM/source reconnects
- Flicker-resistant frame handoff using a local OBS-side frame copy

## Status

The core capture path is working in the current development environment, including VM reconnect, video, and process-isolated audio. This repository is being published early so other VirtualBox/OBS users can help test version differences and edge cases.

There is **no stable release guarantee yet**. Treat every build as experimental.

## Architecture

```text
VirtualBox VM
    |
    | VirtualBox API screenshots / VM state
    v
VBoxCameraBridge.exe
    |
    | Local\\VBoxCameraFrame
    | Local\\VBoxCameraControl
    v
OBS Studio plugin: VBox Camera
    |
    +-- video -> OBS async video source
    +-- audio -> OBS Application Audio Capture
```

The Bridge owns VirtualBox COM/API access. OBS does not directly talk to the VirtualBox COM API. VM information and the selected VM UUID are exchanged through shared memory.

## Repository layout

```text
src/
  bridge/
    VBoxCameraBridge.cpp
  obs/
    plugin-main.c
```

This first alpha publishes the current core source. Build/project integration is still being cleaned up, so setup currently assumes you are comfortable with Visual Studio, CMake, the VirtualBox SDK, and the OBS plugin template.

## Requirements

- Windows 10 or Windows 11
- Oracle VM VirtualBox
- VirtualBox SDK headers (`VirtualBox.h`)
- OBS Studio with Windows Application Audio Capture support
- Visual Studio / MSVC
- CMake for the OBS plugin build

The project is currently Windows-only.

## Building the OBS plugin

The current plugin source is designed to be used with the official OBS plugin template.

1. Clone the official `obsproject/obs-plugintemplate` repository.
2. Replace the template `src/plugin-main.c` with `src/obs/plugin-main.c` from this repository.
3. Configure the template for your OBS version.
4. Build the x64 Windows preset.
5. Copy the resulting plugin DLL into the OBS Studio 64-bit plugin directory.

Example build command used during development:

```bat
cmake --build --preset windows-x64
```

## Building the Bridge

Create/build an x64 C++ project with `src/bridge/VBoxCameraBridge.cpp` and make the VirtualBox SDK headers available so `VirtualBox.h` can be included.

The current source links the standard Windows COM libraries with MSVC pragmas:

```text
Ole32.lib
Uuid.lib
```

The Bridge is currently a console application and must be started manually before opening the VBox Camera properties if you want the VM list to populate.

## Basic usage

1. Start `VBoxCameraBridge.exe`.
2. Start OBS Studio.
3. Add a **VBox Camera** source.
4. Open its properties.
5. Select the VirtualBox VM.
6. Enable **Follow when VM starts** if you want the source to reconnect automatically after the VM is stopped and started again.
7. Choose the aspect ratio and FPS.
8. Leave **Capture VirtualBox Audio** enabled if you want VM audio.

If the VM list is unavailable, start/restart the Bridge and reopen the source properties.

## Alpha limitations / known rough edges

- Bridge startup is manual; automatic background startup is planned.
- Packaging/installer is not implemented yet.
- The Bridge still contains inactive legacy audio-helper code from earlier development; runtime audio capture is handled by OBS in the current path.
- Multi-version compatibility across VirtualBox and OBS releases still needs broader testing.
- The current OBS application-audio target is based on `VirtualBoxVM.exe`; multi-VM audio targeting needs more testing when multiple VMs are simultaneously running.
- Maximum shared video frame allocation is currently 1920x1080; output canvases are currently 960x720 for 4:3 and 1280x720 for 16:9.

## Testing wanted

Useful reports include:

- VirtualBox version
- OBS Studio version
- Windows version
- VM guest OS and resolution
- 30 FPS vs 60 FPS behavior
- VM stop/start reconnect behavior
- scene switching behavior
- long-running audio stability
- behavior with unrelated host audio playing
- behavior with multiple running VMs

When reporting a problem, include the OBS log and Bridge console output if possible.

## Roadmap

Near-term goals include automatic Bridge startup/background operation, cleaner packaging, stronger multi-VM audio targeting, and broader compatibility testing.

## License

VBox Camera Tool is licensed under the MIT License. See [LICENSE](LICENSE).

VirtualBox and OBS Studio are separate projects with their own licenses and trademarks. This repository does not change or replace those licenses.
