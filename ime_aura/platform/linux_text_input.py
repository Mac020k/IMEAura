"""Linux helpers: detect focused / hovered text input via AT-SPI (best effort)."""

from __future__ import annotations

import logging

logger = logging.getLogger(__name__)

_TEXT_ROLES = {
    "text",
    "entry",
    "password text",
    "editable text",
    "document text",
    "paragraph",
    "combo box",
}


def _role_is_text(role: str) -> bool:
    r = role.strip().lower()
    return any(marker in r for marker in _TEXT_ROLES)


def is_text_input_focused() -> bool:
    """Best-effort via `busctl` / AT-SPI tools when available."""
    # accerciser-style: use `python3` Atspi if present is too heavy to spawn each tick.
    # Prefer `busctl` introspection is also heavy. Use `xdotool` focus + role via `at-spi2`.
    try:
        from gi.repository import Atspi  # type: ignore

        desktop = Atspi.get_desktop(0)
        focused = Atspi.get_focus() if hasattr(Atspi, "get_focus") else None
        if focused is None:
            # Walk for STATE_FOCUSED is expensive; try Accessible.get_focus from registry
            try:
                focused = Atspi.get_device(0).get_focus()
            except Exception:
                focused = None
        if focused is None:
            return False
        role_name = focused.get_role_name() or ""
        states = focused.get_state_set()
        if states and states.contains(Atspi.StateType.READ_ONLY):
            return False
        return _role_is_text(role_name)
    except Exception:
        pass

    # Fallback: no reliable detection without AT-SPI bindings
    return False


def is_text_input_hovered() -> bool:
    try:
        from gi.repository import Atspi  # type: ignore
        from PySide6.QtGui import QCursor

        pos = QCursor.pos()
        desktop = Atspi.get_desktop(0)
        # get_accessible_at_point on component interface
        for i in range(desktop.child_count):
            app = desktop.get_child_at_index(i)
            if app is None:
                continue
            try:
                component = app.get_component_iface()
                if component is None:
                    continue
                acc = component.get_accessible_at_point(
                    pos.x(), pos.y(), Atspi.CoordType.SCREEN
                )
                if acc is None:
                    continue
                role_name = acc.get_role_name() or ""
                states = acc.get_state_set()
                if states and states.contains(Atspi.StateType.READ_ONLY):
                    continue
                if _role_is_text(role_name):
                    return True
            except Exception:
                continue
    except Exception as exc:
        logger.debug("Linux hover text-input check failed: %s", exc)
    return False
