"""Combine Bear's kernel commands with CMake's native test commands."""

import json
from pathlib import Path
import sys


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: merge_compile_commands.py KERNEL_DATABASE HOST_DATABASE")

    kernel_path = Path(sys.argv[1])
    host_path = Path(sys.argv[2])
    kernel_commands = json.loads(kernel_path.read_text(encoding="utf-8"))
    host_commands = json.loads(host_path.read_text(encoding="utf-8"))
    if not isinstance(kernel_commands, list) or not isinstance(host_commands, list):
        raise SystemExit("compile databases must contain JSON arrays")

    kernel_path.write_text(
        json.dumps(kernel_commands + host_commands, indent=2) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
