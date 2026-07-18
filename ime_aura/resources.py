"""Resource path resolution for development and PyInstaller builds."""

from __future__ import annotations

import os
import sys


def resource_path(relative_path: str) -> str:
    """Resolve a resource path for both source runs and PyInstaller bundles."""
    try:
        base_path = sys._MEIPASS  # type: ignore[attr-defined]
    except AttributeError:
        # Package lives at ime_aura/; images live at repo root img/
        base_path = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    return os.path.join(base_path, relative_path)
