"""Platform backend protocol for IME state and active screen detection."""

from __future__ import annotations

from typing import Protocol

from PySide6.QtCore import QRect
from PySide6.QtWidgets import QApplication


class PlatformBackend(Protocol):
    """OS-specific operations used by the overlay."""

    def is_japanese_input(self) -> bool:
        """Return True when Japanese (native) input mode is active."""
        ...

    def get_active_screen_geometry(self, app: QApplication) -> QRect | None:
        """Return the geometry of the screen that contains the active window."""
        ...

    def setup_app_identity(self) -> None:
        """Apply platform-specific application identity (e.g. Windows AppUserModelID)."""
        ...

    def is_text_input_focused(self) -> bool:
        """Return True when a text input control currently has keyboard focus."""
        ...

    def is_text_input_hovered(self) -> bool:
        """Return True when the mouse cursor is over a text input control."""
        ...


def geometry_from_point(app: QApplication, x: int, y: int) -> QRect | None:
    """Find the Qt screen geometry that contains the given point."""
    for screen in app.screens():
        if screen.geometry().contains(x, y):
            return screen.geometry()
    return None


def geometry_from_cursor(app: QApplication) -> QRect | None:
    """Fallback: screen under the mouse cursor, else primary screen."""
    cursor_pos = app.primaryScreen().geometry().center()
    try:
        from PySide6.QtGui import QCursor

        cursor_pos = QCursor.pos()
    except Exception:
        pass

    geo = geometry_from_point(app, cursor_pos.x(), cursor_pos.y())
    if geo is not None:
        return geo
    primary = app.primaryScreen()
    return primary.geometry() if primary else None
