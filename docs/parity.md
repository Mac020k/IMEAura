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
- [x] Per-language Aura colors (2–7 slots; ja / zh-Hans / zh-Hant / ko / en / vi / th / km / my / lo) including alpha; reset to defaults
- [x] Gradient width 1–100 px (default 15)
- [x] Display modes: Always / Only while typing (+ optional hover); Aura enable toggle
- [x] “Also show when hovering a text box” (only with Only while typing)
- [x] Text sizes Small (11 pt) / Medium (15 pt) / Large (20 pt)
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
- [x] Firefly: CapsLock Available/Busy toggle, LED, DND (Windows / macOS / Linux X11)
- [x] UI languages: ja / en / zh-Hans / zh-Hant / ko (General → language picker page)
- [x] Settings UI on Windows, macOS (AppKit), and Linux (GTK4)

## Windows settings UI polish

- [x] Scrollable settings when content exceeds window height
- [x] Entrance fade on first show (respects reduced motion)
- [x] Hover option reveal animation when “Only while typing” is selected
- [x] Reset buttons flash 「戻しました」 (~1100 ms)
- [x] Gradient width spin box (click px field to type 1–100)
- [x] Segment control font preview (小 / 中 / 大 at respective point sizes)
- [x] Aura language dropdown (popup menu) per color slot; add/remove (min 2, max 7); current language checked / selected in the menu
- [x] General language button opens in-window language list page

## Settings JSON schema

Defined in `src/core/settings.{h,cpp}`:

- `aura_colors`: array of `{ "lang", "color": [r,g,b,a] }` (2–7; known IDs only)
- `color_jp`, `color_en`: `[r,g,b,a]` 0–255 (legacy read + compatibility write)
- `aura_enabled`: boolean (default `true`); legacy `display_mode=hidden` migrates to `aura_enabled=false`
- `display_mode`: `always` | `on_focus`
- `show_on_hover`: boolean (forced false unless `on_focus`)
- `ui_font_size`: `small` | `medium` | `large`
- `gradient_width`: 1–100
- `language`: `ja` | `en` | `zh-Hans` | `zh-Hant` | `ko`
- `firefly_enabled`: boolean (default `false`)
- `firefly_caps_mode`: `preserve` | `uppercase` | `lowercase` (default `uppercase`)
- `firefly_led_mode`: `auto` | `hid` | `none` (default `auto`)

Paths:

- Windows: `%APPDATA%/IMEAura/settings.json`
- macOS: `~/Library/Application Support/IMEAura/settings.json`
- Linux: `$XDG_CONFIG_HOME/ime_aura/settings.json`

IME language catalog (Must / SEA / Optional): [ime-languages.md](ime-languages.md).

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

Probe JSON includes `ime_lang` and legacy `ime_japanese`.

## Manual visual check

1. Set known colors and width in `settings.json` (or via Aura slots).
2. Launch app → observe edges on multiple monitors and display modes.
3. Switch OS IME among ja / zh / ko / vi / th / SEA layouts / Latin → Aura color follows mapped slots.
4. General → Change language → pick zh-Hans / ko etc. → UI strings update.
5. Add up to 7 color slots; confirm add-slot default colors `#16CC7B`…`#636363`.
6. Verify Firefly still skips Caps remap while Japanese IME is active (`*_is_japanese_input`).
7. macOS / Linux: open settings from status/tray equivalent; edit Aura slots and UI language.

## Firefly compatibility (do not regress)

- Do not modify Firefly state machines / hooks for i18n work.
- Keep `win_is_japanese_input` / `mac_is_japanese_input` / `linux_is_japanese_input` as wrappers over active language == `ja`.
