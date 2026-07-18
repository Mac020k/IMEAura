"""Windows platform backend using IMM32 and Win32 APIs."""

from __future__ import annotations

import ctypes
import ctypes.wintypes

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


class WindowsBackend:
    """IME and active-window detection via Win32 IMM32."""

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

        status = user32.SendMessageW(default_ime_wnd, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0)
        if status == 0:
            return False

        mode = user32.SendMessageW(default_ime_wnd, WM_IME_CONTROL, IMC_GETCONVERSIONMODE, 0)
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
        return win_text_input.is_text_input_focused()

    def is_text_input_hovered(self) -> bool:
        return win_text_input.is_text_input_hovered()
