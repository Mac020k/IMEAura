# IME Aura benchmark methodology

Use this procedure to measure the native C++ build and record results in [parity.md](parity.md).

## Environment

- OS: Windows 11 (primary dev machine)
- Display: single or multi-monitor (note configuration)
- Measurement window: 5 minutes idle after launch, control window closed if testing overlay-only idle

## Metrics

| Metric | Tool (Windows) | Notes |
| --- | --- | --- |
| Private working set | Task Manager → Details → IMEAura.exe | Record peak and steady after 5 min |
| CPU (idle) | Task Manager → Performance, 5 min average | Overlay visible, no user input |
| Wakeups | Not available without ETW | C++ target: no periodic timer in `always` |

## Display modes

Run three sessions with `settings.json` `display_mode` set to:

1. `always` — gradient always visible (default)
2. `on_focus` — text input only (UIA/COM active on Windows)
3. `hidden` — overlay hidden, backend still polling

## C++ targets

| Platform | Private WS (idle) | Idle CPU |
| --- | --- | --- |
| Windows | &lt; 12 MB | ~0% |
| macOS | &lt; 20 MB | ~0% |
| Linux | &lt; 10 MB | ~0% |

## Procedure

1. Close other overlay/IME tools.
2. Set `display_mode` in settings JSON (see [settings paths](../README.md)).
3. Launch app, wait 30 s for stabilization.
4. Close control window (app stays in tray).
5. Record metrics at 1 min and 5 min.
6. Paste results into `parity.md` under **Benchmark results**.
