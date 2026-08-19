# IME Aura parity checklist

Native implementation: C++20 in `src/`.

## Tray / settings behavior

| Item | Behavior |
| --- | --- |
| Close control window | Hides window; app stays in tray/menu bar |
| Re-open settings | Tray / status item → Open Settings |
| Quit | Tray → Quit (confirmation dialog; Cancel does not quit) |

## Functional checklist

- [x] Launch shows edge gradient and control window
- [x] JP / EN colors including alpha; reset to defaults
- [x] Gradient width 1–100 px (default 15)
- [x] Display modes: Always / Only while typing / Hidden
- [x] “Also show when hovering a text box” (only with Only while typing)
- [x] Text sizes Small (11 pt) / Medium (13 pt) / Large (16 pt)
- [x] Click-through overlay, always on top
- [x] Follows display of active window (hover-only follows cursor display)
- [x] Multi-monitor
- [x] Settings persist across restarts (`settings.json`)
- [x] Quit confirmation text; Cancel does not quit
- [x] About shows MIT; no PySide6/Qt in third-party notices
- [x] About dialog: full `LICENSE` + `THIRD_PARTY_NOTICES.md` (scrollable)
- [x] Color picker supports alpha channel (RGBA sliders)
- [x] Gradient thickness clamped to half of monitor min dimension
- [x] Text-input hover/focus updates within ~100 ms (worker callback + 100 ms poll)

## Windows settings UI polish

- [x] Scrollable settings when content exceeds window height
- [x] Entrance fade on first show (respects reduced motion)
- [x] Hover option reveal animation when “Only while typing” is selected
- [x] Reset buttons flash 「戻しました」 (~1100 ms)
- [x] Gradient width spin box (click px field to type 1–100)
- [x] Segment control font preview (小 / 中 / 大 at respective point sizes)

## Settings JSON schema

Defined in `src/core/settings.{h,cpp}`:

- `color_jp`, `color_en`: `[r,g,b,a]` 0–255
- `display_mode`: `always` | `on_focus` | `hidden`
- `show_on_hover`: boolean (forced false unless `on_focus`)
- `ui_font_size`: `small` | `medium` | `large`
- `gradient_width`: 1–100

Paths:

- Windows: `%APPDATA%/IMEAura/settings.json`
- macOS: `~/Library/Application Support/IMEAura/settings.json`
- Linux: `$XDG_CONFIG_HOME/ime_aura/settings.json`

## Automated checks

```bash
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --test-dir build/windows-msvc -C Release
```

Probe mode (no overlay):

```bash
build/windows-msvc/src/Release/IMEAura.exe --probe --json
```

## Manual visual check

1. Set known colors and width in `settings.json`.
2. Launch app → observe edges on multiple monitors and display modes.
3. Verify color, width, fade, and monitor follow behavior.

## Benchmark results

| Build | Mode | Private WS | Idle CPU | Date | Notes |
| --- | --- | --- | --- | --- | --- |
| C++ | always | | | | |
| C++ | on_focus | | | | |
| C++ | hidden | | | | |

See [bench.md](bench.md) for measurement procedure.
