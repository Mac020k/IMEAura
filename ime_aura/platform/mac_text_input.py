"""macOS helpers: detect focused / hovered text input via Accessibility."""

from __future__ import annotations

import ctypes
import ctypes.util
import logging

logger = logging.getLogger(__name__)

_TEXT_ROLES = {
    "AXTextField",
    "AXTextArea",
    "AXSearchField",
    "AXComboBox",
}


class _MacTextInput:
    def __init__(self) -> None:
        self._ready = False
        self._ax = None
        self._cf = None
        self._system = None
        self._attr_focused = None
        self._attr_role = None
        self._init()

    def _init(self) -> None:
        try:
            app_services = ctypes.util.find_library("ApplicationServices")
            cf_path = ctypes.util.find_library("CoreFoundation")
            if not app_services or not cf_path:
                return
            ax = ctypes.cdll.LoadLibrary(app_services)
            cf = ctypes.cdll.LoadLibrary(cf_path)

            ax.AXUIElementCreateSystemWide.restype = ctypes.c_void_p
            ax.AXUIElementCopyAttributeValue.argtypes = [
                ctypes.c_void_p,
                ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_void_p),
            ]
            ax.AXUIElementCopyAttributeValue.restype = ctypes.c_int
            ax.AXUIElementCopyElementAtPosition.argtypes = [
                ctypes.c_void_p,
                ctypes.c_float,
                ctypes.c_float,
                ctypes.POINTER(ctypes.c_void_p),
            ]
            ax.AXUIElementCopyElementAtPosition.restype = ctypes.c_int

            cf.CFRelease.argtypes = [ctypes.c_void_p]
            cf.CFStringCreateWithCString.argtypes = [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.c_uint32,
            ]
            cf.CFStringCreateWithCString.restype = ctypes.c_void_p
            cf.CFStringGetLength.argtypes = [ctypes.c_void_p]
            cf.CFStringGetLength.restype = ctypes.c_long
            cf.CFStringGetCString.argtypes = [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.c_long,
                ctypes.c_uint32,
            ]
            cf.CFStringGetCString.restype = ctypes.c_bool

            k_utf8 = 0x08000100
            self._attr_focused = cf.CFStringCreateWithCString(
                None, b"AXFocusedUIElement", k_utf8
            )
            self._attr_role = cf.CFStringCreateWithCString(None, b"AXRole", k_utf8)
            self._system = ax.AXUIElementCreateSystemWide()
            self._ax = ax
            self._cf = cf
            self._ready = bool(self._system and self._attr_focused and self._attr_role)
        except Exception as exc:
            logger.debug("macOS accessibility init failed: %s", exc)
            self._ready = False

    def _cfstring_to_str(self, cf_str: ctypes.c_void_p) -> str:
        if not cf_str or not self._cf:
            return ""
        length = self._cf.CFStringGetLength(cf_str)
        if length <= 0:
            return ""
        buf_size = (length * 4) + 1
        buf = ctypes.create_string_buffer(buf_size)
        if self._cf.CFStringGetCString(cf_str, buf, buf_size, 0x08000100):
            return buf.value.decode("utf-8", errors="replace")
        return ""

    def _role_of(self, element: ctypes.c_void_p) -> str:
        if not element or not self._ax or not self._attr_role:
            return ""
        value = ctypes.c_void_p()
        if self._ax.AXUIElementCopyAttributeValue(
            element, self._attr_role, ctypes.byref(value)
        ) != 0:
            return ""
        try:
            return self._cfstring_to_str(value)
        finally:
            if value and self._cf:
                self._cf.CFRelease(value)

    def _is_text_role(self, element: ctypes.c_void_p) -> bool:
        return self._role_of(element) in _TEXT_ROLES

    def is_focused(self) -> bool:
        if not self._ready:
            return False
        try:
            value = ctypes.c_void_p()
            if self._ax.AXUIElementCopyAttributeValue(
                self._system, self._attr_focused, ctypes.byref(value)
            ) != 0:
                return False
            try:
                return self._is_text_role(value)
            finally:
                if value and self._cf:
                    self._cf.CFRelease(value)
        except Exception as exc:
            logger.debug("macOS focus check failed: %s", exc)
            return False

    def is_hovered(self) -> bool:
        if not self._ready:
            return False
        try:
            from AppKit import NSEvent  # type: ignore

            loc = NSEvent.mouseLocation()
            # Cocoa y is flipped relative to AXUIElementCopyElementAtPosition
            # AX uses top-left origin of main display in many setups; mouseLocation is bottom-left.
            # Use Quartz screen height if possible.
            x = float(loc.x)
            y = float(loc.y)
            # Convert to top-left based coordinates for AX
            try:
                from AppKit import NSScreen  # type: ignore

                screen_h = float(NSScreen.mainScreen().frame().size.height)
                y = screen_h - y
            except Exception:
                pass

            element = ctypes.c_void_p()
            if self._ax.AXUIElementCopyElementAtPosition(
                self._system, ctypes.c_float(x), ctypes.c_float(y), ctypes.byref(element)
            ) != 0:
                return False
            try:
                return self._is_text_role(element)
            finally:
                if element and self._cf:
                    self._cf.CFRelease(element)
        except Exception:
            # Without PyObjC, fall back to Quartz cursor via CoreGraphics if available
            try:
                return self._is_hovered_quartz()
            except Exception as exc:
                logger.debug("macOS hover check failed: %s", exc)
                return False

    def _is_hovered_quartz(self) -> bool:
        # CGEventSource / CGEventCreate for cursor position without AppKit
        quartz = ctypes.cdll.LoadLibrary(
            ctypes.util.find_library("CoreGraphics") or ctypes.util.find_library("Quartz")
        )
        quartz.CGEventCreate.restype = ctypes.c_void_p
        quartz.CGEventCreate.argtypes = [ctypes.c_void_p]
        quartz.CGEventGetLocation.argtypes = [ctypes.c_void_p]
        # CGPoint return via structure
        class CGPoint(ctypes.Structure):
            _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double)]

        quartz.CGEventGetLocation.restype = CGPoint
        event = quartz.CGEventCreate(None)
        if not event:
            return False
        pt = quartz.CGEventGetLocation(event)
        self._cf.CFRelease(event) if self._cf else None
        element = ctypes.c_void_p()
        if self._ax.AXUIElementCopyElementAtPosition(
            self._system,
            ctypes.c_float(pt.x),
            ctypes.c_float(pt.y),
            ctypes.byref(element),
        ) != 0:
            return False
        try:
            return self._is_text_role(element)
        finally:
            if element and self._cf:
                self._cf.CFRelease(element)


_detector: _MacTextInput | None = None


def _get() -> _MacTextInput:
    global _detector
    if _detector is None:
        _detector = _MacTextInput()
    return _detector


def is_text_input_focused() -> bool:
    return _get().is_focused()


def is_text_input_hovered() -> bool:
    return _get().is_hovered()
