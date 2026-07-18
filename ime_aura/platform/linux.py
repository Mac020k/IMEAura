"""Linux / Unix platform backend using Fcitx5 or IBus."""

from __future__ import annotations

import logging
import os
import shutil
import subprocess

from PySide6.QtCore import QRect
from PySide6.QtWidgets import QApplication

from ime_aura.platform.base import geometry_from_cursor, geometry_from_point

logger = logging.getLogger(__name__)

_JP_ENGINE_MARKERS = (
    "mozc",
    "anthy",
    "kkc",
    "skk",
    "japanese",
    "japan",
    "hiragana",
    "katakana",
)

_EN_ENGINE_MARKERS = (
    "xkb:us",
    "xkb:gb",
    "keyboard-us",
    "keyboard-gb",
    "latin",
    "hangul",  # not Japanese
)


class LinuxBackend:
    """IME via Fcitx5 / IBus; screen via X11 or cursor fallback."""

    def __init__(self) -> None:
        self._provider = self._detect_provider()
        if self._provider is None:
            logger.warning(
                "Neither Fcitx5 nor IBus was detected. "
                "IME state will always report English input."
            )

    def setup_app_identity(self) -> None:
        pass

    def _detect_provider(self) -> str | None:
        if self._fcitx5_available():
            return "fcitx5"
        if self._ibus_available():
            return "ibus"
        return None

    def _fcitx5_available(self) -> bool:
        if shutil.which("fcitx5-remote"):
            return True
        return self._dbus_name_has_owner("org.fcitx.Fcitx5")

    def _ibus_available(self) -> bool:
        if shutil.which("ibus"):
            return True
        return self._dbus_name_has_owner("org.freedesktop.IBus")

    @staticmethod
    def _dbus_name_has_owner(name: str) -> bool:
        if not shutil.which("dbus-send"):
            return False
        try:
            result = subprocess.run(
                [
                    "dbus-send",
                    "--session",
                    "--dest=org.freedesktop.DBus",
                    "--print-reply",
                    "/org/freedesktop/DBus",
                    "org.freedesktop.DBus.NameHasOwner",
                    f"string:{name}",
                ],
                capture_output=True,
                text=True,
                timeout=1.0,
                check=False,
            )
            return "boolean true" in (result.stdout or "").lower()
        except (OSError, subprocess.TimeoutExpired):
            return False

    def is_japanese_input(self) -> bool:
        if self._provider == "fcitx5":
            return self._fcitx5_is_japanese()
        if self._provider == "ibus":
            return self._ibus_is_japanese()
        return False

    def _fcitx5_is_japanese(self) -> bool:
        # State: 0 = closed, 1? inactive, 2 = active (fcitx5-remote)
        state = self._run_cmd(["fcitx5-remote"])
        im_name = self._run_cmd(["fcitx5-remote", "-n"]) or self._dbus_fcitx5_current_im()

        if state is not None:
            state = state.strip()
            # 0 means IME inactive / English-like
            if state == "0":
                return False

        if not im_name:
            # Active state without name: treat as Japanese-capable if state is active
            return state not in (None, "0")

        name = im_name.strip().lower()
        if any(m in name for m in _EN_ENGINE_MARKERS):
            return False
        if any(m in name for m in _JP_ENGINE_MARKERS):
            return True
        # Active non-keyboard IM often means CJK composition
        if state is not None and state.strip() not in ("0",):
            if name.startswith("keyboard-") or name in ("us", "gb"):
                return False
            return True
        return False

    def _dbus_fcitx5_current_im(self) -> str | None:
        if not shutil.which("dbus-send"):
            return None
        try:
            result = subprocess.run(
                [
                    "dbus-send",
                    "--session",
                    "--dest=org.fcitx.Fcitx5",
                    "--print-reply",
                    "/controller",
                    "org.fcitx.Fcitx.Controller1.CurrentInputMethod",
                ],
                capture_output=True,
                text=True,
                timeout=1.0,
                check=False,
            )
            for line in (result.stdout or "").splitlines():
                line = line.strip()
                if line.startswith("string "):
                    return line.split('"')[1] if '"' in line else line[7:].strip()
        except (OSError, subprocess.TimeoutExpired):
            pass
        return None

    def _ibus_is_japanese(self) -> bool:
        engine = self._run_cmd(["ibus", "engine"])
        if not engine:
            return False
        name = engine.strip().lower()
        if any(m in name for m in _EN_ENGINE_MARKERS):
            return False
        if any(m in name for m in _JP_ENGINE_MARKERS):
            # Mozc often appends :jp or similar; exclude ascii-only variants
            if "latin" in name or name.endswith(":us"):
                return False
            return True
        return False

    @staticmethod
    def _run_cmd(cmd: list[str]) -> str | None:
        if not shutil.which(cmd[0]):
            return None
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=1.0,
                check=False,
            )
            if result.returncode != 0:
                return None
            return (result.stdout or "").strip() or None
        except (OSError, subprocess.TimeoutExpired):
            return None

    def get_active_screen_geometry(self, app: QApplication) -> QRect | None:
        point = self._x11_active_window_center()
        if point is not None:
            geo = geometry_from_point(app, point[0], point[1])
            if geo is not None:
                return geo
        return geometry_from_cursor(app)

    def _x11_active_window_center(self) -> tuple[int, int] | None:
        """Use xdotool when available; skip on pure Wayland sessions."""
        session_type = (os.environ.get("XDG_SESSION_TYPE") or "").lower()
        if session_type == "wayland" and not os.environ.get("DISPLAY"):
            return None

        if not shutil.which("xdotool"):
            return None

        try:
            win_id = self._run_cmd(["xdotool", "getactivewindow"])
            if not win_id:
                return None
            geom = self._run_cmd(["xdotool", "getwindowgeometry", "--shell", win_id])
            if not geom:
                return None
            values: dict[str, int] = {}
            for line in geom.splitlines():
                if "=" in line:
                    key, val = line.split("=", 1)
                    try:
                        values[key] = int(val)
                    except ValueError:
                        continue
            x = values.get("X")
            y = values.get("Y")
            w = values.get("WIDTH")
            h = values.get("HEIGHT")
            if None in (x, y, w, h):
                return None
            return x + w // 2, y + h // 2
        except Exception as exc:
            logger.debug("xdotool lookup failed: %s", exc)
            return None
