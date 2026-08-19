<p align="center">
  <img src="img/icon.ico" alt="IME Aura Icon" width="180">
</p>

# IME Aura

IME Aura shows a subtle edge gradient on your display that reflects the current IME state (Japanese vs English input). It helps prevent typing in the wrong input mode.

This repository ships a **native C++20** build (no Qt).

## Features

- Real-time IME state detection (platform-native APIs)
- Lightweight edge overlay (compositor-driven on Windows; edge windows on macOS/Linux)
- Multi-monitor follow (active window; cursor when hover-only)
- Custom JP/EN colors with alpha, gradient width 1–100 px
- Display modes: Always / Only while typing / Hidden (+ optional hover)
- Click-through overlay; settings window + tray/menu-bar icon (close settings without quitting)
- **Firefly** — repurposes CapsLock as a Do Not Disturb toggle (LED on = DND active). Intercepts CapsLock at the low-level hook, drives the LED directly via HID, and controls OS notification suppression (Windows Focus Assist via CloudStore registry)
- **Tabbed settings UI** — Aura / Firefly / General tabs with independent scroll
- **i18n** — Japanese and English UI with instant language switching

## Requirements

| OS | Minimum | Overlay | Settings UI |
| --- | --- | --- | --- |
| Windows | 10 1803+ | Windows.UI.Composition (no D3D device in host) | Win32 + Direct2D / DirectWrite |
| macOS | 11+ | 4× edge `NSWindow` + `CAGradientLayer` | AppKit |
| Linux | Wayland compositor with `wlr-layer-shell` (X11 fallback) | 1×N shm + viewporter | GTK 4 |

### Build tools

- **CMake** 3.25+
- **Windows**: Visual Studio 2026 (or 2022) Build Tools with **Desktop development with C++** and Windows SDK 10.0.22621+
- **macOS**: Xcode CLT, Ninja
- **Linux**: GCC or Clang, Ninja, `pkg-config`, dev packages: `libwayland-dev`, `wayland-protocols`, `libdbus-1-dev`, `libatspi2.0-dev`, `libgtk-4-dev`

## Build (CMake Presets)

```bash
cmake --preset windows-msvc    # or macos-clang / linux-ninja
cmake --build --preset windows-msvc
ctest --test-dir build/windows-msvc -C Release
```

Windows output: `build/windows-msvc/src/Release/IMEAura.exe` (static CRT, no VC++ redistributable).

1. Install extensions from `.vscode/extensions.json` (clangd + CMake Tools). Disable Microsoft C/C++ IntelliSense if prompted.
2. `CMake: Configure` → preset `windows-msvc`
3. Build task **CMake: build (windows-msvc Release)** or F5 with **IMEAura (Windows)**

`compile_commands.json` is copied to the repo root for clangd.

## Run

```bash
./build/windows-msvc/src/Release/IMEAura.exe
```

- Settings open on launch; closing the window hides it (app stays in the tray).
- Quit from tray → **終了** (confirmation dialog).

### Probe mode (no overlay)

```bash
IMEAura.exe --probe --json
```

## Project layout

```
src/core/           settings.json I/O, OverlayPolicy, tokens
src/app/            entry point, app wiring
src/platform/       windows | macos | linux backends
tests/              policy + settings unit tests
docs/parity.md      functional checklist
docs/bench.md       memory/CPU methodology
```

## Settings file

Schema is implemented in `src/core/settings.{h,cpp}`:

- Windows: `%APPDATA%/IMEAura/settings.json`
- macOS: `~/Library/Application Support/IMEAura/settings.json`
- Linux: `$XDG_CONFIG_HOME/ime_aura/settings.json`

## Releases

Pushes to `main` build native artifacts for Windows, Linux, and macOS via GitHub Actions.

## License

MIT License — see `LICENSE`.
