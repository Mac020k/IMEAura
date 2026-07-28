"""Windows platform backend using IMM32 and Win32 APIs."""

from __future__ import annotations

import ctypes
import ctypes.wintypes
import threading
import time

from PySide6.QtCore import QRect
from PySide6.QtWidgets import QApplication

from ime_aura.platform.base import geometry_from_point
from ime_aura.platform import win_text_input

user32 = ctypes.windll.user32
imm32 = ctypes.windll.imm32

WM_IME_CONTROL = 0x0283
IMC_GETOPENSTATUS = 0x0005
IMC_GETCONVERSIONMODE = 0x0001
IME_CMODE_NATIVE = 0x0001
SMTO_BLOCK = 0x0001
SMTO_ABORTIFHUNG = 0x0002
SMTO_ERRORONEXIT = 0x0020
IME_MESSAGE_TIMEOUT_MS = 25
TEXT_INPUT_POLL_INTERVAL_SECONDS = 0.25


def _send_ime_control(hwnd: int, command: int) -> int | None:
    """Send WM_IME_CONTROL with a short timeout so hung IME windows cannot block UI."""
    result = ctypes.c_size_t()
    ok = user32.SendMessageTimeoutW(
        ctypes.wintypes.HWND(hwnd),
        WM_IME_CONTROL,
        command,
        0,
        SMTO_BLOCK | SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT,
        IME_MESSAGE_TIMEOUT_MS,
        ctypes.byref(result),
    )
    if not ok:
        return None
    return int(result.value)


class WindowsBackend:
    """IME and active-window detection via Win32 IMM32."""

    def __init__(self) -> None:
        self._text_input_lock = threading.Lock()
        self._text_input_event = threading.Event()
        self._focus_requested = False
        self._hover_requested = False
        self._focus_inflight = False
        self._hover_inflight = False
        self._focused_result = False
        self._hovered_result = False
        self._focus_completed_at = 0.0
        self._hover_completed_at = 0.0
        threading.Thread(
            target=self._text_input_worker,
            name="imeaura-text-input-worker",
            daemon=True,
        ).start()

    def _text_input_worker(self) -> None:
        """Run MSAA/UIA probes off the UI thread so COM stalls cannot freeze the overlay."""
        while True:
            self._text_input_event.wait()
            while True:
                with self._text_input_lock:
                    run_focus = self._focus_requested
                    run_hover = self._hover_requested
                    self._focus_requested = False
                    self._hover_requested = False
                    self._focus_inflight = run_focus
                    self._hover_inflight = run_hover
                    if not run_focus and not run_hover:
                        self._text_input_event.clear()
                        break

                if run_focus:
                    result = win_text_input.is_text_input_focused()
                    with self._text_input_lock:
                        self._focused_result = result
                        self._focus_completed_at = time.monotonic()
                        self._focus_inflight = False

                if run_hover:
                    result = win_text_input.is_text_input_hovered()
                    with self._text_input_lock:
                        self._hovered_result = result
                        self._hover_completed_at = time.monotonic()
                        self._hover_inflight = False

    def _cached_text_input_result(self, kind: str) -> bool:
        now = time.monotonic()
        with self._text_input_lock:
            if kind == "focus":
                if (
                    not self._focus_requested
                    and not self._focus_inflight
                    and now - self._focus_completed_at >= TEXT_INPUT_POLL_INTERVAL_SECONDS
                ):
                    self._focus_requested = True
                    self._text_input_event.set()
                return self._focused_result

            if (
                not self._hover_requested
                and not self._hover_inflight
                and now - self._hover_completed_at >= TEXT_INPUT_POLL_INTERVAL_SECONDS
            ):
                self._hover_requested = True
                self._text_input_event.set()
            return self._hovered_result

    def setup_app_identity(self) -> None:
        try:
            myappid = "imestateviewer.app.1.0"
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(myappid)
        except Exception:
            pass

    def is_japanese_input(self) -> bool:
        hwnd = user32.GetForegroundWindow()
        if not hwnd:
            return False

        default_ime_wnd = imm32.ImmGetDefaultIMEWnd(hwnd)
        if not default_ime_wnd:
            return False

        status = _send_ime_control(default_ime_wnd, IMC_GETOPENSTATUS)
        if not status:
            return False

        mode = _send_ime_control(default_ime_wnd, IMC_GETCONVERSIONMODE)
        if mode is None:
            return False
        return bool(mode & IME_CMODE_NATIVE)

    def get_active_screen_geometry(self, app: QApplication) -> QRect | None:
        hwnd = user32.GetForegroundWindow()
        if not hwnd:
            return None

        rect = ctypes.wintypes.RECT()
        if not user32.GetWindowRect(hwnd, ctypes.byref(rect)):
            return None

        cx = (rect.left + rect.right) // 2
        cy = (rect.top + rect.bottom) // 2

        geo = geometry_from_point(app, cx, cy)
        if geo is not None:
            return geo

        return geometry_from_point(app, rect.left, rect.top)

    def is_text_input_focused(self) -> bool:
        return self._cached_text_input_result("focus")

    def is_text_input_hovered(self) -> bool:
        return self._cached_text_input_result("hover")
