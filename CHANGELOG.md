# Changelog

All notable changes to VBox Camera Tool will be documented in this file.

The project is currently in alpha, so behavior, configuration, and internal protocols may change between releases.

## [0.1.0-alpha.1] - 2026-08-28

### Added

- Initial public alpha release.
- OBS Studio source named **VBox Camera**.
- VirtualBox VM video capture through the VirtualBox API.
- Shared-memory video transport between Bridge and OBS.
- VM selection from OBS properties.
- VM selection persistence using VirtualBox UUIDs.
- Automatic reconnect when a selected VM stops and starts again.
- 4:3 output mode at 960x720.
- 16:9 output mode at 1280x720.
- Adjustable output frame rate from 1 to 60 FPS.
- Flicker-resistant OBS-side local frame copy.
- VirtualBox process audio capture through OBS Application Audio Capture.
- Isolation from unrelated host audio.
- Automatic audio reinitialization after source/VM reconnects.

### Known limitations

- Windows only.
- Bridge must currently be started manually.
- No installer or automated packaging yet.
- Compatibility across different VirtualBox and OBS versions still needs broader testing.
- Current audio targeting is process-based (`VirtualBoxVM.exe`), so simultaneous multi-VM audio capture needs more testing.
- Shared video allocation currently supports up to 1920x1080 source frames.

### Notes

This release is intended for developers and technical testers. Expect breaking changes and environment-specific issues.
