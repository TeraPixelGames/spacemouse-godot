# SpaceMouse Editor Navigation (Godot 4.x)

This plugin adds 3Dconnexion SpaceMouse 6DoF navigation to the Godot 4.x editor (3D viewport only). It ships with a GDExtension that reads the HID device via HIDAPI and an editor plugin that moves the editor camera while showing a debug overlay.

## Repo Layout
- `spacemouse_native/` – GDExtension source + build scripts
  - `spacemouse_native.gdextension` – engine manifest (points to `bin/<platform>/...`)
  - `src/` – `SpaceMouseDevice` implementation and type registration
  - `thirdparty/hidapi/` – vendored HIDAPI snapshot (cloned from https://github.com/libusb/hidapi)
  - `thirdparty/godot-cpp/` – `godot-cpp` submodule (tag `godot-4.5-stable`)
  - `bin/<platform>/` – build outputs (`.dll` / `.so`)
- `addons/spacemouse_editor/` – Godot editor plugin (`@tool`)

## Prerequisites
- Godot 4.2+ editor
- SCons (for building the native library)
- `godot-cpp` (tag `godot-4.5-stable`) is pulled as a submodule into `spacemouse_native/thirdparty/godot-cpp`.
  - If missing, `SConstruct` auto-clones tag `godot-4.5-stable` from GitHub.
  - Build `godot-cpp` with matching `platform`, `arch`, and `target` (e.g. `scons platform=windows target=template_debug` inside `thirdparty/godot-cpp`).

## Build: Native Library
Outputs land in `spacemouse_native/bin/<platform>/`.

### Windows (MSVC, x64)
```powershell
cd spacemouse_native
scons platform=windows target=template_debug arch=x86_64
scons platform=windows target=template_release arch=x86_64
```
- Produces `bin/windows/spacemouse_native.dll`.
- Ensure `godot-cpp/bin/godot-cpp.windows.<target>.x86_64.lib` exists.

### Linux (GCC/Clang, x64)
```bash
cd spacemouse_native
scons platform=linux target=template_debug arch=x86_64
scons platform=linux target=template_release arch=x86_64
```
- Produces `bin/linux/libspacemouse_native.so`.
- Requires `libudev` and `pthread` (installed on most distros).

### Notes
- HIDAPI is vendored; no extra install needed.
- If you prefer a submodule, replace the folder with `git submodule add https://github.com/libusb/hidapi thirdparty/hidapi`.
- If you add macOS later, extend `SConstruct` with `thirdparty/hidapi/mac/hid.c` and update the `.gdextension`.

## Install & Enable in Godot
1. Copy repo (or `addons/spacemouse_editor` and `spacemouse_native` with built binaries) into your project.
2. Ensure binaries match your editor platform/arch and are at the paths in `spacemouse_native/spacemouse_native.gdextension`.
3. In Godot: `Project > Project Settings > Plugins` → enable **SpaceMouse Editor**.
4. If the native library is missing, the plugin will warn and stay idle.

## Usage
- Tools menu: **SpaceMouse** submenu
  - Toggle **SpaceMouse: Enabled**
  - Toggle **SpaceMouse: Show Debug Overlay**
  - **Print last report (hex)** and **Log current state** for debugging
- The overlay (top-right) shows translation/rotation values, button indices, and last raw report.
- Camera moves while the plugin is enabled and the device is connected:
  - Translation/rotation scales: `trans_scale = 0.0008`, `rot_scale = 0.0006`
  - Damping: `0.85`
- Raw report logging is tied to the debug overlay toggle.

## Troubleshooting
- **No motion / no device found**
  - Unplug/replug the SpaceMouse, then re-toggle “SpaceMouse: Enabled”.
  - On Windows, the 3DxWare driver might grab the device. Quit it or disable its service to let HIDAPI read hidraw.
- **Linux permissions**
  - Add a udev rule (as root) to allow hidraw access, then replug:
    ```
    # /etc/udev/rules.d/99-spacemouse.rules
    SUBSYSTEM=="hidraw", ATTRS{idVendor}=="256f", MODE="0666"
    ```
    Reload with `sudo udevadm control --reload-rules && sudo udevadm trigger`.
- **Debugging reports**
  - Enable the debug overlay to turn on raw logging. Use “Print last report (hex)” and share the output to refine parsing.
- **Thread shutdown**
  - The device reader thread exits on plugin disable/editor quit. If you see hangs, verify the native library was built against your Godot version.

## Assumptions
- Tested against Godot 4.2+ headers; adjust `godot-cpp` if your engine version differs.
- Target platforms: Windows x64, Linux x64; others can be added via `SConstruct` and `.gdextension`.
