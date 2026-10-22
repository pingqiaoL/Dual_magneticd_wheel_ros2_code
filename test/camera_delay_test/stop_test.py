#!/usr/bin/env python3
"""Interrupt only this test runner; its finally block restores original modules."""

import os
import signal
from pathlib import Path

for process in Path('/proc').iterdir():
    if process.name.isdigit():
        try:
            arguments = (process / 'cmdline').read_bytes().split(b'\0')
            if b'/workspace/test/camera_delay_test/run_matrix.py' in arguments:
                os.kill(int(process.name), signal.SIGINT)
                print('Interrupted test runner', process.name)
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            pass
