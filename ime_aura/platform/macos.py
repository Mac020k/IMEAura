"""macOS platform backend using Carbon Text Input Source Services."""

from __future__ import annotations

import ctypes
import ctypes.util
import logging

from PySide6.QtCore import QRect
from PySide6.QtWidgets import QApplication

from ime_aura.platform.base import geometry_from_cursor, geometry_from_point

logger = logging.getLogger(__name__)

# Japanese input source / mode markers (hiragana, katakana, etc.)
_JP_MARKERS = (
    "japanese",
    "kotoeri",
    "hiragana",
    "katakana",
    "apple.inputmethod.japanese",
)

# Explicitly non-Japanese (romaji / ABC within Japanese IME, or English layouts)
_EN_MARKERS = (
    "roman",
    "romaji",
    "abc",
    "keylayout.us",
    "keylayout.british",
    "keylayout.australian",
    "keylayout.canadian",
    "keylayout.irish",
)


def _cfstring_to_str(cf_ptr: ctypes.c_void_p | None) -> str:
    """Convert a CFStringRef to a Python str."""
    if not cf_ptr:
        return ""

    core_foundation = ctypes.cdll.LoadLibrary(ctypes.util.find_library("CoreFoundation"))
    core_foundation.CFStringGetLength.argtypes = [ctypes.c_void_p]
    core_foundation.CFStringGetLength.restype = ctypes.c_long
    core_foundation.CFStringGetCString.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_long,
        ctypes.c_uint32,
    ]
    core_foundation.CFStringGetCString.restype = ctypes.c_bool

    length = core_foundation.CFStringGetLength(cf_ptr)
    if length <= 0:
        return ""

    buf_size = (length * 4) + 1
    buf = ctypes.create_string_buffer(buf_size)
    # kCFStringEncodingUTF8 = 0x08000100
    if core_foundation.CFStringGetCString(cf_ptr, buf, buf_size, 0x08000100):
        return buf.value.decode("utf-8", errors="replace")
    return ""


class MacOSBackend:
    """IME detection via Carbon TIS; screen via Quartz window list."""

    def __init__(self) -> None:
        self._carbon = None
        self._quartz = None
        self._tis_ready = False
        self._init_tis()
        self._init_quartz()

    def _init_tis(self) -> None:
        lib_path = ctypes.util.find_library("Carbon")
        if not lib_path:
            logger.warning("Carbon framework not found; IME detection disabled on macOS")
            return

        carbon = ctypes.cdll.LoadLibrary(lib_path)
        carbon.TISCopyCurrentKeyboardInputSource.restype = ctypes.c_void_p
        carbon.TISCopyCurrentKeyboardInputSource.argtypes = []
        carbon.TISGetInputSourceProperty.restype = ctypes.c_void_p
        carbon.TISGetInputSourceProperty.argtypes = [ctypes.c_void_p, ctypes.c_void_p]

        # Property key constants live in HIToolbox; load as CFString via dlsym-style access
        # We resolve them through Carbon's exported CFString symbols when available.
        try:
            # Fallback: use string property names via CFSTR is hard from ctypes;
            # instead load HIToolbox and look up the global CFStringRefs.
            hitoolbox_path = ctypes.util.find_library("HIToolbox") or lib_path
            hitoolbox = ctypes.CDLL(hitoolbox_path)
            self._prop_source_id = ctypes.c_void_p.in_dll(
                hitoolbox, "kTISPropertyInputSourceID"
            )
            self._prop_mode_id = ctypes.c_void_p.in_dll(
                hitoolbox, "kTISPropertyInputModeID"
            )
        except (ValueError, AttributeError) as exc:
            logger.warning("Failed to load TIS property keys: %s", exc)
            return

        self._carbon = carbon
        self._tis_ready = True

    def _init_quartz(self) -> None:
        lib_path = ctypes.util.find_library("Quartz") or ctypes.util.find_library("CoreGraphics")
        if not lib_path:
            return
        try:
            quartz = ctypes.cdll.LoadLibrary(lib_path)
            quartz.CGWindowListCopyWindowInfo.restype = ctypes.c_void_p
            quartz.CGWindowListCopyWindowInfo.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
            self._quartz = quartz
        except OSError:
            self._quartz = None

    def setup_app_identity(self) -> None:
        pass

    def _current_source_ids(self) -> tuple[str, str]:
        if not self._tis_ready or self._carbon is None:
            return "", ""

        source = self._carbon.TISCopyCurrentKeyboardInputSource()
        if not source:
            return "", ""

        source_id_cf = self._carbon.TISGetInputSourceProperty(source, self._prop_source_id)
        mode_id_cf = self._carbon.TISGetInputSourceProperty(source, self._prop_mode_id)
        source_id = _cfstring_to_str(source_id_cf).lower()
        mode_id = _cfstring_to_str(mode_id_cf).lower()
        return source_id, mode_id

    def is_japanese_input(self) -> bool:
        source_id, mode_id = self._current_source_ids()
        combined = f"{source_id} {mode_id}"

        if any(marker in combined for marker in _EN_MARKERS):
            # Roman / ABC mode of Japanese IME, or English layout
            if "roman" in combined or "romaji" in combined or "abc" in combined:
                return False
            if source_id.startswith("com.apple.keylayout."):
                return False

        if any(marker in combined for marker in _JP_MARKERS):
            return True

        return False

    def get_active_screen_geometry(self, app: QApplication) -> QRect | None:
        point = self._frontmost_window_center()
        if point is not None:
            geo = geometry_from_point(app, point[0], point[1])
            if geo is not None:
                return geo
        return geometry_from_cursor(app)

    def _frontmost_window_center(self) -> tuple[int, int] | None:
        """Best-effort center of the frontmost on-screen window via Quartz."""
        if self._quartz is None:
            return None

        # kCGWindowListOptionOnScreenOnly = 1 << 0
        # kCGNullWindowID = 0
        try:
            # Prefer AppKit for reliable frontmost window when available via objc-free path:
            # use CGWindowList and pick the first layer-0 window that is on screen.
            # CFArray bridging without PyObjC is fragile; use cursor fallback on failure.
            # Attempt a minimal ctypes path via CoreFoundation array count + values.
            cf = ctypes.cdll.LoadLibrary(ctypes.util.find_library("CoreFoundation"))
            cf.CFArrayGetCount.argtypes = [ctypes.c_void_p]
            cf.CFArrayGetCount.restype = ctypes.c_long
            cf.CFArrayGetValueAtIndex.argtypes = [ctypes.c_void_p, ctypes.c_long]
            cf.CFArrayGetValueAtIndex.restype = ctypes.c_void_p
            cf.CFDictionaryGetValue.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
            cf.CFDictionaryGetValue.restype = ctypes.c_void_p
            cf.CFNumberGetValue.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
            cf.CFNumberGetValue.restype = ctypes.c_bool
            cf.CFRelease.argtypes = [ctypes.c_void_p]

            # Create CFString keys for CGWindow keys
            cf.CFStringCreateWithCString.argtypes = [
                ctypes.c_void_p,
                ctypes.c_char_p,
                ctypes.c_uint32,
            ]
            cf.CFStringCreateWithCString.restype = ctypes.c_void_p

            k_utf8 = 0x08000100
            key_layer = cf.CFStringCreateWithCString(None, b"kCGWindowLayer", k_utf8)
            key_bounds = cf.CFStringCreateWithCString(None, b"kCGWindowBounds", k_utf8)

            window_list = self._quartz.CGWindowListCopyWindowInfo(1, 0)
            if not window_list:
                return None

            count = cf.CFArrayGetCount(window_list)
            # kCFNumberIntType = 9
            for i in range(count):
                entry = cf.CFArrayGetValueAtIndex(window_list, i)
                if not entry:
                    continue
                layer_num = cf.CFDictionaryGetValue(entry, key_layer)
                if layer_num:
                    layer_val = ctypes.c_int()
                    if cf.CFNumberGetValue(layer_num, 9, ctypes.byref(layer_val)):
                        if layer_val.value != 0:
                            continue

                bounds = cf.CFDictionaryGetValue(entry, key_bounds)
                if not bounds:
                    continue

                # Bounds is a CFDictionary with X, Y, Width, Height
                key_x = cf.CFStringCreateWithCString(None, b"X", k_utf8)
                key_y = cf.CFStringCreateWithCString(None, b"Y", k_utf8)
                key_w = cf.CFStringCreateWithCString(None, b"Width", k_utf8)
                key_h = cf.CFStringCreateWithCString(None, b"Height", k_utf8)

                def num(key: ctypes.c_void_p) -> float | None:
                    n = cf.CFDictionaryGetValue(bounds, key)
                    if not n:
                        return None
                    # kCFNumberDoubleType = 13
                    val = ctypes.c_double()
                    if cf.CFNumberGetValue(n, 13, ctypes.byref(val)):
                        return float(val.value)
                    return None

                x, y, w, h = num(key_x), num(key_y), num(key_w), num(key_h)
                for k in (key_x, key_y, key_w, key_h):
                    if k:
                        cf.CFRelease(k)

                if None in (x, y, w, h) or w <= 0 or h <= 0:
                    continue
                # Skip tiny menu-bar style windows
                if h < 40:
                    continue

                cf.CFRelease(window_list)
                if key_layer:
                    cf.CFRelease(key_layer)
                if key_bounds:
                    cf.CFRelease(key_bounds)
                return int(x + w / 2), int(y + h / 2)

            cf.CFRelease(window_list)
            if key_layer:
                cf.CFRelease(key_layer)
            if key_bounds:
                cf.CFRelease(key_bounds)
        except Exception as exc:
            logger.debug("Quartz window lookup failed: %s", exc)

        return None
