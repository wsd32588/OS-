#!/usr/bin/env python3
"""Restore missing helper links for live Codex WSL app-server processes.

Run in WSL: python3 tools/repair_codex_wsl.py [--check | --watch]
Only directories referenced by a live, same-user process are considered.
Existing files and links are never replaced. No services are installed.
"""

import argparse
import base64
import os
from pathlib import Path
import subprocess
import sys
import time


HELPERS = ("codex-linux-sandbox", "codex-execve-wrapper", "apply_patch", "applypatch")


def discover_helpers():
    """Read live process metadata rather than guessing hashes or directory names."""
    for process in Path("/proc").iterdir():
        if not process.name.isdecimal():
            continue
        try:
            if process.stat().st_uid != os.getuid():
                continue
            executable = Path(os.readlink(process / "exe"))
            arguments = (process / "cmdline").read_bytes().split(b"\0")
            if executable.name != "codex" or b"app-server" not in arguments:
                continue
            if not executable.is_file() or not os.access(executable, os.X_OK):
                continue
            for descriptor in (process / "fd").iterdir():
                try:
                    target = os.readlink(descriptor)
                except OSError:
                    continue
                if target.endswith(" (deleted)"):
                    target = target[:-10]
                lock = Path(target)
                directory = lock.parent
                if (lock.is_absolute() and lock.name == ".lock"
                        and directory.name.startswith("codex-arg0")
                        and directory.parent.name == "arg0"
                        and directory.parent.parent.name == "tmp"):
                    yield process, executable, directory
        except (OSError, ValueError):
            # Processes can exit while /proc is being read.
            continue


def create_directory(directory):
    try:
        directory.mkdir(mode=0o700)
    except FileExistsError:
        if directory.is_dir():
            return
        # DrvFS can retain a deleted directory inode held open by the server.
        # Windows-side mkdir is the previously validated recovery for this state.
        if not str(directory).startswith("/mnt/"):
            raise
        windows_path = subprocess.check_output(
            ["wslpath", "-w", str(directory)], text=True
        ).strip()
        quoted = windows_path.replace("'", "''")
        command = ("$ErrorActionPreference = 'Stop'; "
                   f"New-Item -ItemType Directory -Force -Path '{quoted}' "
                   "| Out-Null")
        encoded = base64.b64encode(command.encode("utf-16le")).decode("ascii")
        subprocess.run(
            ["powershell.exe", "-NoProfile", "-NonInteractive",
             "-EncodedCommand", encoded], check=True, timeout=20
        )
        if not directory.is_dir():
            raise RuntimeError(f"Windows did not restore directory: {directory}")


def repair_directory(directory, executable, check_only=False):
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise RuntimeError(f"Backend is not executable: {executable}")
    if directory.is_symlink():
        raise RuntimeError(f"Refusing symlink directory: {directory}")
    missing = []
    for name in HELPERS:
        helper = directory / name
        if os.path.lexists(helper):
            if not helper.is_symlink() or helper.resolve() != executable.resolve():
                raise RuntimeError(f"Existing helper conflicts; left untouched: {helper}")
        else:
            missing.append(helper)
    if not missing:
        return "healthy"
    if check_only:
        return "missing: " + ", ".join(path.name for path in missing)
    if not directory.is_dir():
        create_directory(directory)
    for helper in missing:
        # symlink() refuses an existing destination, including a concurrent write.
        helper.symlink_to(executable)
    return "restored: " + ", ".join(path.name for path in missing)


def scan(check_only=False):
    found = False
    failed = False
    seen = set()
    for process, executable, directory in discover_helpers():
        if directory in seen:
            continue
        seen.add(directory)
        found = True
        try:
            # Do not repair paths collected from a process that exited/restarted.
            if Path(os.readlink(process / "exe")) != executable:
                continue
            result = repair_directory(directory, executable, check_only)
            print(f"PID {process.name}: {directory}: {result}", flush=True)
            if check_only and result.startswith("missing:"):
                failed = True
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            print(f"PID {process.name}: {error}", file=sys.stderr, flush=True)
            failed = True
    if not found:
        print("No live Codex app-server helper directories found.", flush=True)
    return 1 if failed or not found else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="Inspect without changing files")
    mode.add_argument("--watch", action="store_true", help="Keep repairing until Ctrl+C")
    parser.add_argument("--interval", type=float, default=10,
                        help="Watch interval in seconds (minimum 1; default 10)")
    options = parser.parse_args()
    if not sys.platform.startswith("linux"):
        parser.error("Run this script with Python inside WSL/Linux.")
    if not 1 <= options.interval <= 3600:
        parser.error("--interval must be between 1 and 3600 seconds")
    if not options.watch:
        return scan(options.check)
    try:
        while True:
            scan()
            time.sleep(options.interval)
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
