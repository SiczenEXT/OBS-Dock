# OBS Control Dock

A native OBS Studio dock for launching saved actions. It uses standard Qt widgets and does not apply a custom stylesheet, so it inherits OBS's active theme, colours, fonts, widget metrics, and native styling.

## Features

- Adds **Control Dock** to the OBS Docks menu.
- Five toolbar buttons: add, delete, properties, move up, and move down.
- Left-click a control to run it; right-click a control to select it for editing, deleting, or reordering.
- Stores controls in the OBS module configuration folder (`controls.json`).
- Automatic actions for scene switching, scene-item visibility, source enable/mute, scene/source filters, streaming, recording, replay buffer, virtual camera, screenshots, Studio Mode, and transitions.
- Manual mode records one key combination and triggers the matching OBS hotkey binding.

## Windows build

The repository includes a GitHub Actions workflow that builds a Windows x64 DLL against the OBS 33 SDK. Pushes to `main` start a build automatically; the workflow can also be started from **Actions → Build OBS Control Dock (Windows) → Run workflow**.

The build uses OBS Project's plugin-template CMake helpers pinned to commit `3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc`. SDK and dependency versions are pinned with SHA-256 hashes in `buildspec.json`.

The workflow publishes:

- `obs-control-dock-windows-x64.zip` — ready-to-copy plugin folder.
- `obs-control-dock.dll` — the module itself.

## Install on OBS Studio 33 (Windows x64)

1. Close OBS Studio.
2. Download `obs-control-dock-windows-x64.zip` from the repository's **Releases** page.
3. Extract the `obs-control-dock` folder into `C:\ProgramData\obs-studio\plugins\`.
4. For portable OBS, extract it into `<OBS folder>\plugins\` instead.
5. Check that the final layout is `plugins\obs-control-dock\obs-control-dock.dll` and `plugins\obs-control-dock\data\...`.
6. Start OBS and open **Docks → Control Dock**.

OBS 33 uses the new plugin folder layout. Older `bin\64bit` layout is not used in the release archive.

## Controls

1. Press **+** and enter a name.
2. Choose **Automatic** and select an OBS action, or choose **Manual** and record a hotkey.
3. For Manual mode, assign the same key combination to an OBS hotkey in **Settings → Hotkeys**.
4. Left-click to execute. Right-click first to select a control for the other toolbar buttons.

Manual hotkeys trigger OBS's registered hotkey system; they do not send keystrokes to other applications. Multi-step sequences are not supported.

## Build requirements

- OBS Studio 33 SDK and matching Qt 6 dependencies (downloaded by the CMake bootstrap).
- Visual Studio 2026 with Windows 11 SDK 10.0.26100.0.
- CMake 4.2 or newer.

The workflow builds the plugin on GitHub-hosted Windows runners; the development container used to prepare the source does not include the Windows SDK or OBS runtime, so runtime loading still needs to be checked on an OBS installation.

## License

GPL-2.0-or-later.
