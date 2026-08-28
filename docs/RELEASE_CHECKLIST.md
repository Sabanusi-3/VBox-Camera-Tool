# Release checklist

## v0.1.0-alpha.1

This is an alpha release intended for developers and technical testers.

### 1. Build from the public repository

Do not package an older local source tree by accident. Clone or pull the current `main` branch first, then build the public sources.

Verify that the Bridge and OBS plugin compile successfully as x64 builds.

### 2. Smoke test

Before packaging, verify at minimum:

- OBS starts with `vbox-camera.dll` installed.
- **VBox Camera** appears in the OBS source list.
- Bridge starts and publishes the VM list.
- A VM can be selected in source properties.
- Video appears correctly.
- 4:3 mode works.
- 16:9 mode works.
- 30 FPS works.
- 60 FPS works.
- VirtualBox audio is captured when enabled.
- Unrelated host audio is not captured by VBox Camera.
- Stopping the selected VM disconnects cleanly.
- Starting it again reconnects video.
- Starting it again reconnects audio.
- Closing/reopening OBS or recreating the source does not leave audio permanently silent.

### 3. Package

Recommended archive name:

```text
VBox-Camera-Tool-v0.1.0-alpha.1-win64.zip
```

Recommended archive layout:

```text
VBox-Camera-Tool/
  VBoxCameraBridge.exe
  obs-plugins/
    64bit/
      vbox-camera.dll
  README.txt
  LICENSE.txt
```

`README.txt` should make it clear that this is an alpha build, that the Bridge currently needs to be started manually, and where the DLL must be installed.

Do not include build directories, PDB files, Visual Studio caches, local SDK copies, VirtualBox binaries, OBS binaries, personal logs, or machine-specific configuration files in the release archive.

### 4. Release metadata

Tag:

```text
v0.1.0-alpha.1
```

Release title:

```text
VBox Camera Tool v0.1.0-alpha.1
```

Mark the GitHub release as a **pre-release**.

### 5. Suggested release notes

```markdown
# VBox Camera Tool v0.1.0-alpha.1

First public alpha of VBox Camera Tool.

VBox Camera Tool adds a dedicated **VBox Camera** source to OBS Studio for capturing VirtualBox virtual machines without relying on normal desktop/window capture.

## Included in this alpha

- VirtualBox VM video capture
- Dedicated OBS source
- VM selection and UUID persistence
- Automatic VM stop/start reconnect
- 4:3 (960x720) and 16:9 (1280x720) output
- Adjustable 1-60 FPS output
- VirtualBox process-specific audio capture through OBS
- Isolation from unrelated host audio
- Automatic audio reconnect handling
- Flicker-resistant shared-memory frame handoff

## Important

This is development-stage **ALPHA software**. It is intended for developers and technical testers and may contain bugs or breaking changes.

The Bridge currently needs to be started manually before using VBox Camera. Installation is also manual in this release.

## Testing feedback wanted

Please include your Windows, VirtualBox, and OBS Studio versions when reporting problems. VM restart/reconnect behavior, 60 FPS behavior, long-running audio, and multiple-VM environments are especially useful test cases.

See the repository README and CHANGELOG for current limitations and setup information.
```

### 6. Final check before Publish release

Open the ZIP you intend to upload and test the files from that ZIP, not only the original build output. This catches missing DLLs/files and accidental packaging mistakes.

Then create the tag/release from the tested commit, attach the ZIP, select **Set as a pre-release**, and publish.
