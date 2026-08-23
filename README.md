<p align="center">
  <img src="img/icon.ico" alt="IME Aura Icon" width="180">
</p>

# IME Aura

[日本語](README-ja.md)

**IME Aura** is a lightweight desktop utility that draws a subtle edge gradient on your display to show the active IME / input language (Japanese, Chinese, Korean, or Latin). It helps you avoid typing in the wrong input mode.

The app is a native **C++20** project. Overlays and settings UIs use each OS’s native APIs.

| Platform | Overlay | Settings UI |
| --- | --- | --- |
| **Windows** 10 1803+ | Windows.UI.Composition | Win32 + Direct2D / DirectWrite |
| **macOS** 11+ | Edge `NSWindow` + `CAGradientLayer` | AppKit |
| **Linux** | Wayland `wlr-layer-shell` (X11 fallback) | GTK 4 |

## Features

- Real-time IME state detection via platform APIs
- Click-through edge overlay (always on top)
- Multi-monitor follow (active window; cursor when hover-only)
- Custom per-language edge colors (up to 7 slots; Phase 1: ja / zh-Hans / zh-Hant / ko / en); gradient width 1–100 px
- Display modes: Always / Only while typing / Hidden (+ optional hover)
- Tabbed settings (Aura / Firefly / General) with UI in Japanese, English, Simplified/Traditional Chinese, and Korean
- Language picker page under General; Aura color rows use language dropdowns
- Catalog of future IME languages: [docs/ime-languages.md](docs/ime-languages.md)
- Tray / menu-bar icon — closing settings hides the window; the app keeps running
- **Firefly**: CapsLock as Available / Busy (Do Not Disturb) with LED + notification suppression (Windows, macOS, Linux/X11)

## Install

### Prebuilt binaries (recommended)

1. Open the [Releases](https://github.com/Mac020k/IMEAura/releases) page.
2. Download the zip for your OS:
   - `IMEAura-windows-x64.zip`
   - `IMEAura-macos-arm64.zip`
   - `IMEAura-linux-x64.zip`
3. Extract and run:
   - **Windows:** `IMEAura.exe` (static CRT — no Visual C++ redistributable required)
   - **macOS:** `IMEAura.app`
   - **Linux:** `./IMEAura`

Pushes to `main` publish new artifacts via GitHub Actions.

### Build from source

#### Requirements

| Tool / OS | Notes |
| --- | --- |
| **CMake** | 3.25+ |
| **Windows** | Visual Studio 2026 (or 2022) Build Tools with *Desktop development with C++*; Windows SDK 10.0.22621+ |
| **macOS** | Xcode Command Line Tools, Ninja |
| **Linux** | GCC or Clang, Ninja, `pkg-config`, plus: `libwayland-dev`, `wayland-protocols`, `libdbus-1-dev`, `libatspi2.0-dev`, `libgtk-4-dev` |

#### Configure, build, test

```bash
# Choose your preset: windows-msvc | macos-clang | linux-ninja
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --test-dir build/windows-msvc -C Release --output-on-failure
```

| Preset | Output binary |
| --- | --- |
| `windows-msvc` | `build/windows-msvc/src/Release/IMEAura.exe` |
| `macos-clang` | `build/macos-clang/src/IMEAura` |
| `linux-ninja` | `build/linux-ninja/src/IMEAura` |

#### VS Code / Cursor

1. Install extensions from `.vscode/extensions.json` (clangd + CMake Tools). Disable Microsoft C/C++ IntelliSense if prompted.
2. **CMake: Configure** → preset `windows-msvc` (or your platform preset).
3. Build with **CMake: build (windows-msvc Release)** or press F5 with **IMEAura (Windows)**.

`compile_commands.json` is copied to the repo root for clangd when available.

## Run

```bash
# Windows
./build/windows-msvc/src/Release/IMEAura.exe

# macOS / Linux
./build/macos-clang/src/IMEAura
./build/linux-ninja/src/IMEAura
```

- Settings open on first launch. Closing the window hides it; the app stays in the tray / menu bar.
- Quit from the tray → **終了** / **Quit** (confirmation dialog).

### Probe mode (diagnostics, no overlay)

```bash
IMEAura.exe --probe --json
```

## Settings

Stored as JSON (`src/core/settings.{h,cpp}`):

| OS | Path |
| --- | --- |
| Windows | `%APPDATA%/IMEAura/settings.json` |
| macOS | `~/Library/Application Support/IMEAura/settings.json` |
| Linux | `$XDG_CONFIG_HOME/ime_aura/settings.json` (default `~/.config/ime_aura/`) |

| Key | Values | Default |
| --- | --- | --- |
| `aura_colors` | `[{"lang":"ja\|zh-Hans\|zh-Hant\|ko\|en","color":[r,g,b,a]}, …]` (2–7) | ja + en defaults |
| `color_jp` / `color_en` | `[r,g,b,a]` 0–255 | Written for compatibility; prefer `aura_colors` |
| `display_mode` | `always` \| `on_focus` \| `hidden` | `always` |
| `show_on_hover` | bool (only with `on_focus`) | `false` |
| `ui_font_size` | `small` \| `medium` \| `large` | `medium` |
| `gradient_width` | 1–100 | `15` |
| `language` | `ja` \| `en` \| `zh-Hans` \| `zh-Hant` \| `ko` | `ja` |
| `firefly_enabled` | bool | `false` |
| `firefly_caps_mode` | `preserve` \| `uppercase` \| `lowercase` | `uppercase` |
| `firefly_led_mode` | `auto` \| `hid` \| `none` | `auto` |

When adding a color slot beyond the default ja/en pair, default colors are applied in order: `#16CC7B`, `#F1D60F`, `#E6690C`, `#7E43D5`, `#636363`.

## Firefly (Windows, macOS, Linux/X11)

Firefly remaps the physical **CapsLock** key to an Available / Busy (Do Not Disturb) toggle while IME Aura is running.

| State | Meaning | CapsLock LED (if LED control is on) | Notifications |
| --- | --- | --- | --- |
| **Available** | Default after enable | Off | Normal |
| **Busy** | After CapsLock press | On | Suppressed |

- Enabling Firefly always starts in **Available**.
- Each CapsLock press toggles Available ↔ Busy.
- While Firefly is on, CapsLock no longer toggles system letter case; case follows `firefly_caps_mode`.
- Disabling Firefly restores the previous CapsLock and DND settings when possible.
- Fail-closed: if the platform intercept cannot be installed, enable is refused (`firefly_enabled` cleared).

| Platform | Intercept | LED | DND |
| --- | --- | --- | --- |
| **Windows** | `WH_KEYBOARD_LL` | CapsLock bit / HID | Registry Quiet Hours |
| **macOS** | `CGEventTap` (Accessibility permission) | CapsLock state | `defaults` Notification Center UI |
| **Linux** | X11 `XGrabKey` + XTest | sysfs or XKB indicator | GNOME `gsettings` (when writable) |

See [docs/firefly.md](docs/firefly.md) for architecture and per-OS requirements. On macOS and Linux, configure Firefly via `settings.json` until settings UI parity lands.

### CapsLock state (`firefly_caps_mode`)

| Value | Effect |
| --- | --- |
| `preserve` | Keep CapsLock case polarity from just before enable |
| `uppercase` | Letters default uppercase; Shift inverts |
| `lowercase` | Letters default lowercase; Shift inverts |

Japanese IME composition is passed through. Ctrl / Alt / Win shortcuts are not remapped.

### LED mode (`firefly_led_mode`)

| Value | Effect |
| --- | --- |
| `auto` | Prefer native CapsLock LED as Busy lamp; on Linux also tries sysfs `*capslock` |
| `hid` | Drive LED via HID/sysfs only (Windows HID report; Linux sysfs) |
| `none` | Do not drive CapsLock LED for Busy / Available |

On Windows, CapsLock LED and letter case share one toggle bit. Firefly’s default path uses that bit as the Busy lamp and rewrites Latin A–Z so case follows `firefly_caps_mode` XOR Shift.

## Project layout

```
src/app/            entry point and app wiring
src/core/           settings, policy, i18n, Firefly state machine
src/platform/       windows | macos | linux backends
tests/              unit tests (settings, policy, i18n, Firefly, layout)
docs/parity.md      functional checklist
docs/bench.md       memory / CPU measurement notes
```

## License

MIT License — see [LICENSE](LICENSE). Third-party notices: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
