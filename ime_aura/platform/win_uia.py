"""Windows UI Automation helpers for focused / hovered text input."""

from __future__ import annotations

import ctypes
import logging
from ctypes import HRESULT, POINTER, WINFUNCTYPE, byref, c_int, c_ulong, c_void_p

logger = logging.getLogger(__name__)

ole32 = ctypes.windll.ole32

UIA_ControlTypePropertyId = 30003
UIA_LocalizedControlTypePropertyId = 30004
UIA_NamePropertyId = 30005
UIA_ClassNamePropertyId = 30012
UIA_AutomationIdPropertyId = 30011
UIA_IsKeyboardFocusablePropertyId = 30009
UIA_HasKeyboardFocusPropertyId = 30008
UIA_IsPasswordPropertyId = 30019
UIA_AriaRolePropertyId = 30101

UIA_ValuePatternId = 10002
UIA_TextPatternId = 10014
UIA_TextPattern2Id = 10024

UIA_EditControlTypeId = 50004
UIA_ComboBoxControlTypeId = 50003
UIA_SpinnerControlTypeId = 50016
UIA_DocumentControlTypeId = 50030
UIA_CustomControlTypeId = 50025
UIA_GroupControlTypeId = 50026
UIA_PaneControlTypeId = 50033

_EDITABLE_CONTROL_TYPES = frozenset(
    {
        UIA_EditControlTypeId,
        UIA_ComboBoxControlTypeId,
        UIA_SpinnerControlTypeId,
        # Document is handled separately (browser page roots are also Document)
    }
)

_ARIA_TEXT_ROLES = frozenset(
    {
        "textbox",
        "searchbox",
        "combobox",
        "search",
    }
)

_CLASSNAME_TEXT_MARKERS = (
    "textarea",
    "textbox",
    "text-input",
    "textinput",
    "inputarea",
    "monaco-editor",
    "monaco-mouse-cursor-text",
    "cm-content",
    "cm-editor",
    "ace_editor",
    "ql-editor",
    "composer-bar editor",
)

VT_I4 = 3
VT_BOOL = 11
VT_BSTR = 8


class _GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", c_ulong),
        ("Data2", ctypes.c_ushort),
        ("Data3", ctypes.c_ushort),
        ("Data4", ctypes.c_ubyte * 8),
    ]


class _VARIANT(ctypes.Structure):
    _fields_ = [
        ("vt", ctypes.c_ushort),
        ("r1", ctypes.c_ushort),
        ("r2", ctypes.c_ushort),
        ("r3", ctypes.c_ushort),
        ("data", ctypes.c_ulonglong),
    ]


class _POINT(ctypes.Structure):
    _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double)]


def _parse_guid(text: str) -> _GUID:
    parts = text.strip("{}").split("-")
    data4 = bytes.fromhex(parts[3] + parts[4])
    return _GUID(
        int(parts[0], 16),
        int(parts[1], 16),
        int(parts[2], 16),
        (ctypes.c_ubyte * 8).from_buffer_copy(data4),
    )


_CLSID_CUIAutomation = _parse_guid("{ff48dba4-60ef-4201-aa87-54103eef594e}")
_IID_IUIAutomation = _parse_guid("{30cbe57d-d9d0-452a-ab13-7ac5ac4825ee}")

_uia = None
_uia_vtbl = None
_initialized = False


def _ensure_uia() -> bool:
    global _uia, _uia_vtbl, _initialized
    if _initialized:
        return _uia is not None
    _initialized = True
    try:
        ole32.CoInitialize(None)
    except Exception:
        pass
    try:
        obj = c_void_p()
        ole32.CoCreateInstance.restype = HRESULT
        hr = ole32.CoCreateInstance(
            byref(_CLSID_CUIAutomation),
            None,
            1,  # CLSCTX_INPROC_SERVER
            byref(_IID_IUIAutomation),
            byref(obj),
        )
        if hr != 0 or not obj.value:
            logger.debug("CoCreateInstance(CUIAutomation) failed: 0x%08x", hr & 0xFFFFFFFF)
            return False
        _uia = obj
        _uia_vtbl = ctypes.cast(ctypes.cast(obj, POINTER(c_void_p))[0], POINTER(c_void_p))
        return True
    except Exception as exc:
        logger.debug("UIA init failed: %s", exc)
        return False


def _release(punk: c_void_p | None) -> None:
    if not punk or not punk.value:
        return
    try:
        vtbl = ctypes.cast(ctypes.cast(punk, POINTER(c_void_p))[0], POINTER(c_void_p))
        WINFUNCTYPE(c_ulong, c_void_p)(vtbl[2])(punk)
    except Exception:
        pass


def _variant_i4(var: _VARIANT) -> int | None:
    if var.vt == VT_I4:
        return ctypes.c_long.from_buffer_copy(ctypes.c_ulonglong(var.data)).value
    return None


def _variant_bool(var: _VARIANT) -> bool | None:
    if var.vt == VT_BOOL:
        return bool(ctypes.c_short.from_buffer_copy(ctypes.c_ulonglong(var.data)).value)
    return None


def _variant_bstr(var: _VARIANT) -> str | None:
    if var.vt != VT_BSTR or not var.data:
        return None
    try:
        oleaut32 = ctypes.windll.oleaut32
        ptr = ctypes.c_void_p(var.data)
        length = int(oleaut32.SysStringLen(ptr))
        if length <= 0:
            return ""
        return ctypes.wstring_at(ptr, length)
    except Exception:
        return None


def _classname_suggests_text_input(class_name: str) -> bool:
    n = class_name.lower().strip()
    if not n:
        return False
    if any(marker in n for marker in _CLASSNAME_TEXT_MARKERS):
        return True
    # standalone token "editor" (e.g. "composer-bar editor")
    tokens = n.replace("-", " ").replace("_", " ").split()
    return "editor" in tokens


def _element_info(el: c_void_p) -> dict:
    """Return control type / patterns for a UIA element."""
    info = {
        "control_type": None,
        "focusable": None,
        "has_focus": None,
        "has_value_pattern": False,
        "has_text_pattern": False,
        "class_name": None,
        "aria_role": None,
        "localized_type": None,
        "is_text_input": False,
    }
    if not el or not el.value:
        return info

    el_vtbl = ctypes.cast(ctypes.cast(el, POINTER(c_void_p))[0], POINTER(c_void_p))
    get_prop = WINFUNCTYPE(HRESULT, c_void_p, c_int, POINTER(_VARIANT))(el_vtbl[10])
    get_pattern = WINFUNCTYPE(HRESULT, c_void_p, c_int, POINTER(c_void_p))(el_vtbl[16])
    oleaut32 = ctypes.windll.oleaut32
    oleaut32.VariantClear.argtypes = [POINTER(_VARIANT)]

    def read_prop(pid: int) -> _VARIANT:
        var = _VARIANT()
        get_prop(el, pid, byref(var))
        return var

    var = read_prop(UIA_ControlTypePropertyId)
    info["control_type"] = _variant_i4(var)
    oleaut32.VariantClear(byref(var))

    var = read_prop(UIA_IsKeyboardFocusablePropertyId)
    info["focusable"] = _variant_bool(var)
    oleaut32.VariantClear(byref(var))

    var = read_prop(UIA_HasKeyboardFocusPropertyId)
    info["has_focus"] = _variant_bool(var)
    oleaut32.VariantClear(byref(var))

    var = read_prop(UIA_ClassNamePropertyId)
    info["class_name"] = _variant_bstr(var)
    oleaut32.VariantClear(byref(var))

    var = read_prop(UIA_AriaRolePropertyId)
    info["aria_role"] = _variant_bstr(var)
    oleaut32.VariantClear(byref(var))

    var = read_prop(UIA_LocalizedControlTypePropertyId)
    info["localized_type"] = _variant_bstr(var)
    oleaut32.VariantClear(byref(var))

    for pattern_id, key in (
        (UIA_ValuePatternId, "has_value_pattern"),
        (UIA_TextPatternId, "has_text_pattern"),
        (UIA_TextPattern2Id, "has_text_pattern"),
    ):
        punk = c_void_p()
        if get_pattern(el, pattern_id, byref(punk)) == 0 and punk.value:
            info[key] = True
            _release(punk)

    control_type = info["control_type"]
    aria = (info["aria_role"] or "").lower().strip()
    class_name = info["class_name"] or ""

    if control_type in _EDITABLE_CONTROL_TYPES:
        info["is_text_input"] = True
    elif control_type == UIA_DocumentControlTypeId:
        # Notepad etc. expose RichEdit class; browser page roots are bare "document"
        cn = class_name.lower()
        if "richedit" in cn or _classname_suggests_text_input(class_name):
            info["is_text_input"] = True
        elif aria in _ARIA_TEXT_ROLES:
            info["is_text_input"] = True
        elif aria == "document" and not cn.strip():
            info["is_text_input"] = False
        elif cn.strip() and info["has_text_pattern"] and info["has_value_pattern"]:
            info["is_text_input"] = True
        else:
            info["is_text_input"] = False
    elif info["has_text_pattern"]:
        info["is_text_input"] = True
    elif aria in _ARIA_TEXT_ROLES:
        info["is_text_input"] = True
    elif _classname_suggests_text_input(class_name):
        info["is_text_input"] = True
    elif (
        info["has_value_pattern"]
        and info["focusable"]
        and control_type == UIA_CustomControlTypeId
    ):
        info["is_text_input"] = True

    return info


def focused_element_is_text_input() -> tuple[bool, dict]:
    if not _ensure_uia() or _uia is None or _uia_vtbl is None:
        return False, {"error": "uia_unavailable"}

    get_focused = WINFUNCTYPE(HRESULT, c_void_p, POINTER(c_void_p))(_uia_vtbl[8])
    el = c_void_p()
    hr = get_focused(_uia, byref(el))
    if hr != 0 or not el.value:
        return False, {"error": "no_focused_element", "hr": hr}
    try:
        info = _element_info(el)
        return bool(info["is_text_input"]), info
    finally:
        _release(el)


def element_at_point_is_text_input(x: int, y: int) -> tuple[bool, dict]:
    if not _ensure_uia() or _uia is None or _uia_vtbl is None:
        return False, {"error": "uia_unavailable"}

    # ElementFromPoint is vtable index 7; takes POINT with doubles on some defs —
    # actually tagPOINT uses LONG. UI Automation uses POINT with long x,y.
    class POINT_LONG(ctypes.Structure):
        _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]

    element_from_point = WINFUNCTYPE(
        HRESULT, c_void_p, POINT_LONG, POINTER(c_void_p)
    )(_uia_vtbl[7])
    el = c_void_p()
    hr = element_from_point(_uia, POINT_LONG(int(x), int(y)), byref(el))
    if hr != 0 or not el.value:
        return False, {"error": "no_element_at_point", "hr": hr}
    try:
        info = _element_info(el)
        return bool(info["is_text_input"]), info
    finally:
        _release(el)
