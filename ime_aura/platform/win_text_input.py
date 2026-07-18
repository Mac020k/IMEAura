"""Windows helpers: detect focused / hovered text input controls."""

from __future__ import annotations

import ctypes
import ctypes.wintypes
import logging

logger = logging.getLogger(__name__)

user32 = ctypes.windll.user32
oleacc = ctypes.oledll.oleacc
ole32 = ctypes.windll.ole32

OBJID_CLIENT = ctypes.c_int(-4).value  # 0xFFFFFFFC as signed for AccessibleObjectFromWindow

ROLE_SYSTEM_TEXT = 0x2A
ROLE_SYSTEM_DOCUMENT = 0x0F
ROLE_SYSTEM_SPINBUTTON = 0x34
ROLE_SYSTEM_COMBOBOX = 0x2E
ROLE_SYSTEM_EDIT = ROLE_SYSTEM_TEXT

STATE_SYSTEM_READONLY = 0x40
STATE_SYSTEM_UNAVAILABLE = 0x1
STATE_SYSTEM_FOCUSABLE = 0x100000
STATE_SYSTEM_INVISIBLE = 0x8000

GWL_STYLE = -16
ES_READONLY = 0x0800
CBS_DROPDOWN = 0x0002
CBS_DROPDOWNLIST = 0x0003

CHILDID_SELF = 0

_EDIT_CLASS_MARKERS = (
    "edit",
    "richedit",
    "richedit20",
    "richedit50",
    "richedit60",
    "scintilla",
    "textfield",
    "passwordbox",
    # WinUI / modern text input hosts
    "inputsite",
    "windows.ui.input.inputsite",
)


class _GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", ctypes.c_ulong),
        ("Data2", ctypes.c_ushort),
        ("Data3", ctypes.c_ushort),
        ("Data4", ctypes.c_ubyte * 8),
    ]


class _POINT(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]


class _GUITHREADINFO(ctypes.Structure):
    _fields_ = [
        ("cbSize", ctypes.c_ulong),
        ("flags", ctypes.c_ulong),
        ("hwndActive", ctypes.wintypes.HWND),
        ("hwndFocus", ctypes.wintypes.HWND),
        ("hwndCapture", ctypes.wintypes.HWND),
        ("hwndMenuOwner", ctypes.wintypes.HWND),
        ("hwndMoveSize", ctypes.wintypes.HWND),
        ("hwndCaret", ctypes.wintypes.HWND),
        ("rcCaret", ctypes.wintypes.RECT),
    ]


# IAccessible IID
_IID_IAccessible = _GUID(
    0x618736E0,
    0x3C3D,
    0x11CF,
    (ctypes.c_ubyte * 8)(0x81, 0x0C, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71),
)


class _IAccessibleVtbl(ctypes.Structure):
    _fields_ = [
        ("QueryInterface", ctypes.c_void_p),
        ("AddRef", ctypes.c_void_p),
        ("Release", ctypes.c_void_p),
        ("GetTypeInfoCount", ctypes.c_void_p),
        ("GetTypeInfo", ctypes.c_void_p),
        ("GetIDsOfNames", ctypes.c_void_p),
        ("Invoke", ctypes.c_void_p),
        ("get_accParent", ctypes.c_void_p),
        ("get_accChildCount", ctypes.c_void_p),
        ("get_accChild", ctypes.c_void_p),
        ("get_accName", ctypes.c_void_p),
        ("get_accValue", ctypes.c_void_p),
        ("get_accDescription", ctypes.c_void_p),
        ("get_accRole", ctypes.c_void_p),
        ("get_accState", ctypes.c_void_p),
    ]


class _IAccessible(ctypes.Structure):
    _fields_ = [("lpVtbl", ctypes.POINTER(_IAccessibleVtbl))]


# VARIANT for role/state out params
class _VARIANT(ctypes.Structure):
    _fields_ = [
        ("vt", ctypes.c_ushort),
        ("wReserved1", ctypes.c_ushort),
        ("wReserved2", ctypes.c_ushort),
        ("wReserved3", ctypes.c_ushort),
        ("data", ctypes.c_ulonglong),
    ]


VT_I4 = 3
VT_EMPTY = 0

_ole_initialized = False


def _ensure_ole() -> bool:
    global _ole_initialized
    if _ole_initialized:
        return True
    try:
        hr = ole32.CoInitialize(None)
        # S_OK=0, S_FALSE=1, RPC_E_CHANGED_MODE=0x80010106 already init differently
        _ole_initialized = True
        return True
    except Exception:
        return False


def _class_name(hwnd: int) -> str:
    if not hwnd:
        return ""
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, 256)
    return buf.value


def _window_style(hwnd: int) -> int:
    return int(user32.GetWindowLongW(hwnd, GWL_STYLE))


def _is_edit_class(hwnd: int) -> bool:
    name = _class_name(hwnd).lower()
    if not name:
        return False
    if any(marker in name for marker in _EDIT_CLASS_MARKERS):
        style = _window_style(hwnd)
        if style & ES_READONLY:
            return False
        return True
    # Editable combo box (not dropdown-list only)
    if "combobox" in name:
        style = _window_style(hwnd) & 0x0F
        return style == CBS_DROPDOWN
    return False


def _release_accessible(acc: ctypes.POINTER(_IAccessible) | None) -> None:
    if not acc:
        return
    try:
        vtbl = acc.contents.lpVtbl.contents
        release = ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(vtbl.Release)
        release(acc)
    except Exception:
        pass


def _variant_i4(var: _VARIANT) -> int | None:
    if var.vt == VT_I4:
        return ctypes.c_long.from_buffer_copy(ctypes.c_ulonglong(var.data)).value
    # Sometimes role comes as VT_BSTR; ignore
    return None


def _acc_role_state(acc: ctypes.POINTER(_IAccessible)) -> tuple[int | None, int | None]:
    vtbl = acc.contents.lpVtbl.contents
    get_role = ctypes.WINFUNCTYPE(
        ctypes.HRESULT,
        ctypes.c_void_p,
        _VARIANT,
        ctypes.POINTER(_VARIANT),
    )(vtbl.get_accRole)
    get_state = ctypes.WINFUNCTYPE(
        ctypes.HRESULT,
        ctypes.c_void_p,
        _VARIANT,
        ctypes.POINTER(_VARIANT),
    )(vtbl.get_accState)

    child = _VARIANT()
    child.vt = VT_I4
    child.data = CHILDID_SELF

    role_var = _VARIANT()
    state_var = _VARIANT()
    role = None
    state = None
    try:
        if get_role(acc, child, ctypes.byref(role_var)) == 0:
            role = _variant_i4(role_var)
        if get_state(acc, child, ctypes.byref(state_var)) == 0:
            state = _variant_i4(state_var)
    except Exception:
        return None, None
    return role, state


def _is_editable_role(role: int | None, state: int | None) -> bool:
    if role is None:
        return False
    if state is not None:
        if state & STATE_SYSTEM_UNAVAILABLE:
            return False
        if state & STATE_SYSTEM_READONLY:
            return False
        if state & STATE_SYSTEM_INVISIBLE:
            return False

    if role == ROLE_SYSTEM_TEXT:
        return True
    if role == ROLE_SYSTEM_SPINBUTTON:
        return True
    if role in (ROLE_SYSTEM_DOCUMENT, ROLE_SYSTEM_COMBOBOX):
        # Prefer controls that can take keyboard focus
        if state is None:
            return True
        return bool(state & STATE_SYSTEM_FOCUSABLE)
    return False


def _accessible_from_hwnd(hwnd: int) -> ctypes.POINTER(_IAccessible) | None:
    if not hwnd or not _ensure_ole():
        return None
    acc = ctypes.POINTER(_IAccessible)()
    try:
        hr = oleacc.AccessibleObjectFromWindow(
            ctypes.wintypes.HWND(hwnd),
            OBJID_CLIENT,
            ctypes.byref(_IID_IAccessible),
            ctypes.byref(acc),
        )
        if hr != 0 or not acc:
            return None
        return acc
    except Exception:
        return None


def _accessible_from_point(x: int, y: int) -> tuple[ctypes.POINTER(_IAccessible) | None, int]:
    if not _ensure_ole():
        return None, 0
    acc = ctypes.POINTER(_IAccessible)()
    child = _VARIANT()
    pt = _POINT(x, y)
    try:
        hr = oleacc.AccessibleObjectFromPoint(
            pt,
            ctypes.byref(acc),
            ctypes.byref(child),
        )
        if hr != 0 or not acc:
            return None, 0
        return acc, 0
    except Exception:
        return None, 0


def _hwnd_is_text_input(hwnd: int) -> bool:
    if not hwnd:
        return False
    if _is_edit_class(hwnd):
        return True
    acc = _accessible_from_hwnd(hwnd)
    if not acc:
        return False
    try:
        role, state = _acc_role_state(acc)
        return _is_editable_role(role, state)
    finally:
        _release_accessible(acc)


def get_focus_hwnd() -> int:
    hwnd_fg = user32.GetForegroundWindow()
    if not hwnd_fg:
        return 0
    info = _GUITHREADINFO()
    info.cbSize = ctypes.sizeof(_GUITHREADINFO)
    tid = user32.GetWindowThreadProcessId(hwnd_fg, None)
    if user32.GetGUIThreadInfo(tid, ctypes.byref(info)) and info.hwndFocus:
        return int(info.hwndFocus)
    return int(hwnd_fg)


def deepest_hwnd_from_point(x: int, y: int) -> int:
    pt = ctypes.wintypes.POINT(x, y)
    hwnd = user32.WindowFromPoint(pt)
    if not hwnd:
        return 0

    # Walk into child windows under the point
    parent = hwnd
    while True:
        parent_rect = ctypes.wintypes.RECT()
        if not user32.GetWindowRect(parent, ctypes.byref(parent_rect)):
            break
        local = ctypes.wintypes.POINT(x - parent_rect.left, y - parent_rect.top)
        # CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT
        child = user32.ChildWindowFromPointEx(parent, local, 0x0007)
        if not child or child == parent:
            break
        parent = child
    return int(parent)


def is_text_input_focused() -> bool:
    try:
        hwnd = get_focus_hwnd()
        if _is_edit_class(hwnd):
            return True

        try:
            from ime_aura.platform import win_uia

            uia_result, _ = win_uia.focused_element_is_text_input()
            if uia_result:
                return True
        except Exception:
            pass

        if hwnd and _ensure_ole():
            acc = _accessible_from_hwnd(hwnd)
            if acc:
                try:
                    role, state = _acc_role_state(acc)
                    return _is_editable_role(role, state)
                finally:
                    _release_accessible(acc)
        return False
    except Exception as exc:
        logger.debug("focus text-input check failed: %s", exc)
        return False


def is_text_input_hovered() -> bool:
    try:
        pt = ctypes.wintypes.POINT()
        if not user32.GetCursorPos(ctypes.byref(pt)):
            return False
        hwnd = deepest_hwnd_from_point(pt.x, pt.y)
        if _is_edit_class(hwnd):
            return True
        try:
            from ime_aura.platform import win_uia

            ok, _ = win_uia.element_at_point_is_text_input(pt.x, pt.y)
            return ok
        except Exception:
            return False
    except Exception as exc:
        logger.debug("hover text-input check failed: %s", exc)
        return False
