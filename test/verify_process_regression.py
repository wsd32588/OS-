#!/usr/bin/env python3
"""Run the complete process/FD payload in an isolated copy of the current kernel."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "test/process_regression"
SOURCE_DIRECTORIES = ("include", "kernel", "user", "drivers", "lib", "freestanding")
FINAL_MARKER = b"[process regression] PASS final free35 alloc21 net14 reap3 pool0 two S ticks"


def replace_one(path, old, new):
    text = path.read_text()
    if text.count(old) != 1:
        raise ValueError(f"{path}: expected one anchor {old!r}, found {text.count(old)}")
    path.write_text(text.replace(old, new, 1))


def source_hashes():
    paths = [ROOT / "Makefile", ROOT / "linker.ld"]
    for directory in SOURCE_DIRECTORIES:
        paths.extend(path for path in (ROOT / directory).rglob("*")
                     if path.is_file() and "build" not in path.parts and
                     path.suffix not in (".o", ".d", ".elf", ".bin", ".su"))
    return {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(paths)}


def prepare_workspace(output):
    workspace = Path(tempfile.mkdtemp(prefix="source-", dir=output))
    ignore = shutil.ignore_patterns("*.o", "*.d", "*.elf", "*.bin", "*.su", "build", "__pycache__")
    for directory in SOURCE_DIRECTORIES:
        shutil.copytree(ROOT / directory, workspace / directory, ignore=ignore)
    for name in ("Makefile", "linker.ld"):
        shutil.copyfile(ROOT / name, workspace / name)
    for name in ("main.c", "worker.c"):
        shutil.copyfile(FIXTURE / name, workspace / "user" / name)
    factory = workspace / "kernel/boot/private/boot_tasks.cpp"
    if not factory.exists():
        raise ValueError("current kernel has no boot_tasks.cpp factory seam")
    shutil.copyfile(FIXTURE / "boot_tasks.cpp", factory)
    shutil.copyfile(FIXTURE / "boot_file_checks.inc", factory.parent / "boot_file_checks.inc")
    shutil.copyfile(FIXTURE / "probe.hpp", workspace / "include/process_regression_probe.hpp")
    shutil.copyfile(FIXTURE / "probe.cpp", workspace / "kernel/boot/process_regression_probe.cpp")
    replace_one(factory, '#include "boot_private.h"',
                '#include "boot_private.h"\n#include "process_regression_probe.hpp"')
    replace_one(factory, "    return sched_configure_user_reaper(ids[1]);",
                "    const int result = sched_configure_user_reaper(ids[1]);\n"
                "    if (result == 0) { regression_after_boot(); }\n    return result;")
    scheduler = workspace / "kernel/task/sched.cpp"
    replace_one(scheduler, '#include "task/sched.h"',
                '#include "task/sched.h"\n#include "process_regression_probe.hpp"')
    replace_one(scheduler, "    wake_sleeping_tasks();",
                "    regression_on_timer(frame);\n    wake_sleeping_tasks();")
    with scheduler.open("a") as stream:
        stream.write("\n" + (FIXTURE / "sched_observers.cpp").read_text())
    with (workspace / "kernel/fs/file_service.cpp").open("a") as stream:
        stream.write("\n" + (FIXTURE / "service_observer.cpp").read_text())
    makefile = workspace / "Makefile"
    replace_one(makefile, "\nOBJS = \\\n", "\nOBJS = \\\n    kernel/boot/process_regression_probe.o \\\n")
    wrappers = " ".join("--wrap=" + name for name in (
        "pmm_alloc_page", "pmm_free_page", "user_address_space_release", "task_reap",
        "process_spawn_builtin", "sched_fork_current", "sched_exec_current", "syscall_handle"))
    replace_one(makefile, "$(LD) -T linker.ld $(OBJS) -o $@",
                "$(LD) " + wrappers + " -T linker.ld $(OBJS) -o $@")
    return workspace


def run_qemu(command, timeout):
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, start_new_session=True)
    output = bytearray()
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    deadline = time.monotonic() + timeout
    reason = "timeout"
    try:
        while time.monotonic() < deadline:
            for key, _ in selector.select(min(0.2, max(0, deadline - time.monotonic()))):
                chunk = os.read(key.fileobj.fileno(), 65536)
                if not chunk:
                    reason = "qemu exited"
                    return bytes(output), reason
                output.extend(chunk)
            if FINAL_MARKER in output:
                reason = "final marker observed"
                break
    finally:
        selector.close()
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
        tail, _ = process.communicate(timeout=5)
        output.extend(tail)
    return bytes(output), reason


def check_output(output):
    text = output.decode(errors="replace")
    expected_once = (
        "[process regression] PASS boot two S and two U",
        "[process regression] PASS release U0 seven pages",
        "[process regression] PASS release SPAWN4 seven pages",
        "[process regression] PASS release EXEC-old5 seven pages",
        "[process regression] PASS release EXEC-new5 seven pages",
        "[process regression] PASS release root3 seven pages",
        "[process regression] PASS SPAWN4 seven allocations",
        "[process regression] PASS FORK5 seven allocations",
        "[process regression] PASS EXEC5 seven allocations and frees",
        FINAL_MARKER.decode(),
        "[fork child] snapshot OK", "[U1] fork isolation OK",
        "[fs] FD model OK", "[fs] ELF lookup/read OK",
        "[fs] /etc/about: TinyOS builtin fs\n",
        "[task] user exit id=0x0000000000000002 status=0x0000000000000000",
        "[task] user exit id=0x0000000000000004 status=0x0000000000000017",
        "[task] user exit id=0x0000000000000005 status=0x0000000000000025",
        "[task] user exit id=0x0000000000000003 status=0x0000000000000000",
    )
    checks = {marker: text.count(marker) == 1 for marker in expected_once}
    for marker in ("[worker] fresh image OK", "[worker] file read OK", "[worker] fd read OK"):
        checks[marker + " twice"] = text.count(marker) == 2
    checks["three reaps without PMM"] = text.count("[process regression] PASS reap zero PMM") == 3
    checks["no guest/probe failure"] = not any(marker in text for marker in (
        "[process regression] FAIL", "[user fault]", "=== TRAP ===", "kernel halt",
        "[write] failed", "unexpected child exit result", "exec returned", "Task creation failed"))
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--timeout", type=float, default=35)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    output = (args.output_dir or ROOT / ".cache/process-regression" /
              (time.strftime("%Y%m%d-%H%M%S") + f"-{os.getpid()}")).resolve()
    if output.exists():
        parser.error("output directory already exists; use a new path to preserve evidence")
    output.mkdir(parents=True)
    before = source_hashes()
    (output / "formal-before.json").write_text(json.dumps(before, indent=2) + "\n")
    report = {"all_passed": False, "checks": {}, "output_dir": str(output)}
    try:
        workspace = prepare_workspace(output)
        report["workspace"] = str(workspace)
        build_command = ["make", "-j4", "KERNEL_TRACE=1", "CONSOLE_MODE=kdbg"]
        build = subprocess.run(build_command, cwd=workspace, capture_output=True, text=True, timeout=120)
        build_log = build.stdout + build.stderr
        (output / "build.log").write_text(build_log)
        if build.returncode:
            raise ValueError("isolated build failed; see build.log")
        undefined = subprocess.run(["riscv64-linux-gnu-nm", "-u", str(workspace / "kernel.elf")],
                                   capture_output=True, text=True, check=True)
        (output / "undefined.log").write_text(undefined.stdout + undefined.stderr)
        command = ["qemu-system-riscv64", "-machine", "virt", "-m", "128M", "-nographic",
                   "-bios", "default", "-kernel", str(workspace / "kernel.elf")]
        (output / "commands.log").write_text("cwd=" + str(workspace) + "\n" +
                                             " ".join(build_command) + "\n" + " ".join(command) + "\n")
        guest, reason = run_qemu(command, args.timeout)
        (output / "qemu.log").write_bytes(guest)
        checks = check_output(guest)
        checks["isolated build succeeded"] = build.returncode == 0
        report["build_warnings"] = [line for line in build_log.splitlines() if "warning:" in line]
        checks["no undefined symbols"] = not undefined.stdout.strip()
        report.update(checks=checks, stop_reason=reason,
                      observed_totals={"image_releases": 5, "legal_unique_page_frees": 35,
                                       "runtime_allocations": 21, "net_available_pages": 14,
                                       "reaps_without_PMM": 3, "final_service_pool_in_use": 0,
                                       "post_exit_S_timer_ticks_at_least": 2}
                      if checks[FINAL_MARKER.decode()] else None)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
    finally:
        after = source_hashes()
        (output / "formal-after.json").write_text(json.dumps(after, indent=2) + "\n")
        report["formal_source_count"] = len(before)
        report["formal_changes"] = sorted(name for name in before.keys() | after.keys()
                                          if before.get(name) != after.get(name))
        report["checks"]["formal sources unchanged"] = before == after
        report["all_passed"] = "error" not in report and all(report["checks"].values())
        report["limitations"] = (
            "Only the isolated copy uses saved regression payloads and the two-S/two-U boot factory. "
            "Production modules execute unchanged except read-only observation hooks and link wrappers. "
            "The original one-page RX assertion remains enabled. No dependence on cached probe code, "
            "no 500-round/OOM/boundary matrix or console input simulation. QEMU is stopped externally "
            "after the final accounting marker or timeout; this is not guest shutdown.")
        (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{'[PASS]' if report['all_passed'] else '[FAIL]'} process regression: {output / 'results.json'}")
    if not report["all_passed"]:
        print(report.get("error", "failed checks: " + repr([name for name, passed in report["checks"].items() if not passed])))
    return 0 if report["all_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
