# Firefly cross-platform architecture

Firefly remaps physical **CapsLock** to an **Available / Busy** toggle while IME Aura runs. Portable logic lives in `src/core/firefly.{h,cpp}`; OS-specific hooks, LEDs, and Do Not Disturb (DND) live in per-platform backends behind `FireflyBackend` (`src/platform/firefly_backend.h`). Lifecycle wiring is shared via `FireflyHost` (`src/platform/firefly_host.{h,cpp}`).

## Layering

```
Settings (JSON) ──► FireflyHost ──► FireflyBackend (per OS)
                         │                    │
                         │                    ├── key intercept + remap
                         │                    ├── LED (Busy lamp)
                         │                    └── DND / notification mute
                         ▼
              core/firefly evaluate_firefly()
              (pure state machine)
```

| Layer | Path | Role |
| --- | --- | --- |
| State machine | `src/core/firefly.*` | Available ↔ Busy transitions, caps-mode math |
| Interface | `src/platform/firefly_backend.h` | `start/stop`, LED, DND, `capabilities()`, optional `poll()` |
| Lifecycle | `src/platform/firefly_host.*` | Enable/disable from settings; fail-closed on hook failure |
| Windows | `src/platform/windows/win_firefly.cpp` | `WH_KEYBOARD_LL`, HID LED, registry Quiet Hours |
| macOS | `src/platform/macos/mac_firefly.mm` | `CGEventTap`, CapsLock state LED, `defaults` DND |
| Linux | `src/platform/linux/linux_firefly.cpp` | X11 grab + XTest, sysfs LED, GNOME `gsettings` DND |

## Requirements by platform

### Shared (all OSes)

- CapsLock press toggles Available ↔ Busy; enable always starts Available.
- Latin A–Z remapped per `firefly_caps_mode` XOR Shift when not composing Japanese and no Ctrl/Alt/Super held.
- Japanese IME composition passes through.
- Disabling Firefly restores prior CapsLock state and backed-up DND when possible.
- Fail-closed: if the intercept hook/tap/grab cannot be installed, `firefly_enabled` is cleared and startup refuses enable.

### Windows

| Capability | Mechanism |
| --- | --- |
| Key intercept | Low-level keyboard hook (`WH_KEYBOARD_LL`) |
| Letter remap | `SendInput` with inject tag |
| LED `auto` | CapsLock toggle bit |
| LED `hid` | HID keyboard LED output report |
| DND | Registry Quiet Hours profile swap + `WpnUserService_*` restart |

### macOS

| Capability | Mechanism |
| --- | --- |
| Key intercept | `CGEventTap` on session (requires **Accessibility** permission) |
| Letter remap | `CGEventKeyboardSetUnicodeString` with inject tag |
| LED `auto` | CapsLock key state via synthetic key events |
| LED `hid` | Not supported (no HID path in this backend) |
| DND | `defaults -currentHost write com.apple.notificationcenterui doNotDisturb` with backup/restore |

### Linux (X11)

| Capability | Mechanism |
| --- | --- |
| Key intercept | `XGrabKey` on root for CapsLock + A–Z |
| Letter remap | `XTestFakeKeyEvent` |
| LED `auto` | CapsLock XKB indicator or sysfs `*capslock` brightness |
| LED `hid` | sysfs `/sys/class/leds/*capslock/brightness` |
| DND | GNOME `gsettings org.gnome.desktop.notifications disable-notifications` with backup/restore |

Wayland-only sessions without X11 are not supported for Firefly yet (no compositor key grab). Use an X11 session or XWayland for Firefly on Linux.

## Settings keys

See `src/core/settings.{h,cpp}`:

- `firefly_enabled`
- `firefly_caps_mode`: `preserve` | `uppercase` | `lowercase`
- `firefly_led_mode`: `auto` | `hid` | `none`
- `firefly_busy_action`: `dnd` | `keep_awake` | `voice_input` | `meeting` | `hands_free` (default `dnd`)
- `firefly_keep_display_on`: bool — only used when `firefly_busy_action` is `keep_awake`

On macOS and Linux, configure Firefly via `settings.json` until native settings UI parity is implemented.

## Busy actions

When Firefly enters **Busy**, the CapsLock LED turns on and the selected action applies:

| Key | Busy effect |
| --- | --- |
| `dnd` | Suppress notifications (default) |
| `keep_awake` | Prevent idle sleep (`firefly_keep_display_on` optionally keeps the display on) |
| `voice_input` | Trigger OS voice typing once on Busy entry |
| `meeting` | Suppress notifications + mute microphone |
| `hands_free` | Suppress notifications + trigger voice typing |

Leaving Busy or disabling Firefly restores backed-up system state when possible.

### Platform support

| Action | Windows | macOS | Linux (X11) |
| --- | --- | --- | --- |
| `dnd` | Yes | Yes | GNOME gsettings |
| `keep_awake` | `SetThreadExecutionState` | `IOPMAssertion` | `systemd-inhibit` (when available) |
| `voice_input` | `Win+H` inject | Dictation shortcut (Fn×2 default) | Not supported |
| `meeting` | DND + mic mute | DND + mic mute | Not supported |
| `hands_free` | DND + voice | DND + voice | Not supported |

## Capabilities probe

Each backend implements `FireflyCapabilities`:

- `can_intercept_caps` — hook/tap/grab installed
- `can_drive_led` — LED path available
- `can_set_dnd` — DND write path probed successfully
- `can_keep_awake` — sleep prevention available
- `can_mute_mic` — default capture device mute available
- `can_trigger_voice_input` — OS voice typing shortcut can be injected

Windows settings UI surfaces these; unsupported busy actions are grayed out in the picker.
