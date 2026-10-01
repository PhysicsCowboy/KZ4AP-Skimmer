"""Keeps Windows from running a long prototype run in its power-saving mode (Task 11p).

Windows 11 power-throttles a process it considers background work (EcoQoS: on a hybrid CPU it is run on the
efficiency cores at their most efficient clock). Started from the agent's shell, the prototype is such a
process: on this i7-12700H a fresh process ran the duration fit at about 2 ms per observation, and 2-4 s later
at 8-13 ms, and a pure-Python loop slowed by the same factor of about 4.4 (measured, Task 11p report). Opting
the process out restores full speed; it changes no result, only how fast the same arithmetic runs.

Call disable_power_throttling() once in every process that decodes (the runner's main process and each
worker's initializer). It is a no-op on other systems."""

from __future__ import annotations

import sys

_PROCESS_POWER_THROTTLING = 4             # PROCESS_INFORMATION_CLASS ProcessPowerThrottling
_THROTTLING_CURRENT_VERSION = 1
_THROTTLING_EXECUTION_SPEED = 0x1


def disable_power_throttling() -> bool:
    """Opts this process out of execution-speed throttling (SetProcessInformation, ProcessPowerThrottling with
    ControlMask = EXECUTION_SPEED and StateMask = 0, "always run at full speed"). Returns True if Windows
    accepted it, False elsewhere or on failure (including a Windows without SetProcessInformation)."""
    if sys.platform != "win32":
        return False
    try:
        return _set_execution_speed_unthrottled()
    except (AttributeError, OSError):
        return False


def _set_execution_speed_unthrottled() -> bool:
    import ctypes
    from ctypes import wintypes

    class _State(ctypes.Structure):
        _fields_ = [("Version", wintypes.ULONG), ("ControlMask", wintypes.ULONG), ("StateMask", wintypes.ULONG)]

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.GetCurrentProcess.restype = wintypes.HANDLE
    kernel32.SetProcessInformation.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    kernel32.SetProcessInformation.restype = wintypes.BOOL
    state = _State(_THROTTLING_CURRENT_VERSION, _THROTTLING_EXECUTION_SPEED, 0)
    return bool(kernel32.SetProcessInformation(kernel32.GetCurrentProcess(), _PROCESS_POWER_THROTTLING,
                                               ctypes.byref(state), ctypes.sizeof(state)))
