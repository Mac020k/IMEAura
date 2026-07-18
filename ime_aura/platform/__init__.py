"""Factory for OS-specific platform backends."""

from __future__ import annotations

import sys

from ime_aura.platform.base import PlatformBackend


def create_backend() -> PlatformBackend:
    """Create the platform backend for the current OS."""
    if sys.platform == "win32":
        from ime_aura.platform.windows import WindowsBackend

        return WindowsBackend()
    if sys.platform == "darwin":
        from ime_aura.platform.macos import MacOSBackend

        return MacOSBackend()
    # Linux and other Unix-like systems
    from ime_aura.platform.linux import LinuxBackend

    return LinuxBackend()
