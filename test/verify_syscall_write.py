#!/usr/bin/env python3
"""QEMU verification for the WRITE syscall, one scenario at a time.

A single temporary workspace is built once and reused.  Later scenarios only
overwrite kernel/user_entry.S, so make does an incremental rebuild.
"""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[1]

EXPECTED_BASELINE = b"Hello from user!\n"
WRITE_BEGIN = b"\x03"
WRITE_END = b"\x04"
RETURN_OK = b"\x01"
RETURN_FAILED = b"\x02"
FORBIDDEN_MARKERS = (
    b"[write] failed",
    b"[user fault]",
    b"kernel halt",
)

USER_IMAGE_TEMPLATE = """#include "syscall.h"

.section .rodata.user_image, "a", @progbits
.balign 4
.option push
.option norvc
.option norelax

.global user_image_start
.global user_image_end

user_image_start:
{setup}
    # PUTCHAR changes a0: preserve the WRITE arguments around the marker.
    mv s1, a0
    mv s2, a1
    li a0, 3
    li a7, SYSCALL_PUTCHAR
    ecall

    mv a0, s1
    mv a1, s2
    li a7, SYSCALL_WRITE
    ecall
    mv s0, a0

    li a0, 4
    li a7, SYSCALL_PUTCHAR
    ecall

    li t0, {expected_return}
    bne s0, t0, .Lfail
    j .Lok

.Lok:
    li a0, 1
    li a7, SYSCALL_PUTCHAR
    ecall
    li a0, 0
    li a7, SYSCALL_EXIT
    ecall
    j .Lhalt

.Lfail:
    li a0, 2
    li a7, SYSCALL_PUTCHAR
    ecall
    li a0, 1
    li a7, SYSCALL_EXIT
    ecall

.Lhalt:
    j .Lhalt

{data}
user_image_end:
.option pop
"""


def copy_project(destination: Path) -> None:
    for name in ("include", "kernel", "drivers", "user"):
        shutil.copytree(
            REPO / name,
            destination / name,
            ignore=shutil.ignore_patterns("*.o", "*.d", "build"),
        )
    for name in ("Makefile", "linker.ld"):
        shutil.copy2(REPO / name, destination / name)


def build(workspace: Path) -> None:
    result = subprocess.run(
        ["make", "-j4"],
        cwd=workspace,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=60,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        raise SystemExit("kernel build failed")


def run_qemu(workspace: Path, timeout: float = 8.0) -> bytes:
    process = subprocess.Popen(
        [
            "qemu-system-riscv64",
            "-machine", "virt",
            "-m", "128M",
            "-nographic",
            "-bios", "default",
            "-kernel", "kernel.elf",
        ],
        cwd=workspace,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    try:
        try:
            output, _ = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                output, _ = process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                output, _ = process.communicate()
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()
    return output


def check_common(output: bytes) -> None:
    for marker in FORBIDDEN_MARKERS:
        if marker in output:
            raise SystemExit(f"unexpected marker {marker!r}")
    if b"[syscall] exit" not in output:
        raise SystemExit("user task did not reach exit")
    after_exit = output.partition(b"[syscall] exit")[2]
    if after_exit.count(b"[timer] tick in S-mode") < 2:
        raise SystemExit("timer/scheduler did not continue after user exit")


def run_baseline(workspace: Path) -> None:
    build(workspace)
    output = run_qemu(workspace)
    if EXPECTED_BASELINE not in output:
        raise SystemExit(f"missing expected output {EXPECTED_BASELINE!r}")
    check_common(output)


def run_generated(workspace: Path, scenario: dict) -> None:
    image = USER_IMAGE_TEMPLATE.format(
        setup=scenario["setup"],
        expected_return=scenario["expected_return"],
        data=scenario.get("data", ""),
    )
    (workspace / "kernel" / "user_entry.S").write_text(image)
    build(workspace)
    output = run_qemu(workspace)

    if output.count(RETURN_OK) != 1:
        raise SystemExit("user did not report success (missing 0x01)")
    if RETURN_FAILED in output:
        raise SystemExit("user reported a0 mismatch (found 0x02)")

    if output.count(WRITE_BEGIN) != 1 or output.count(WRITE_END) != 1:
        raise SystemExit("missing or repeated WRITE boundary markers")
    _, _, after_begin = output.partition(WRITE_BEGIN)
    actual_bytes, end_marker, after_end = after_begin.partition(WRITE_END)
    if not end_marker or RETURN_OK not in after_end:
        raise SystemExit("WRITE/return markers are out of order")
    expected_bytes = scenario["expected_bytes"]
    if actual_bytes != expected_bytes:
        raise SystemExit(
            f"WRITE bytes differ: actual={actual_bytes!r}, expected={expected_bytes!r}"
        )

    check_common(output)

SEPARATE_STACK_ALLOCATION = """
    void* gap_page = pmm_alloc_page();
    if (gap_page == NULL) {
        result = SV39_ERR_NO_MEMORY;
        goto clean_up;
    }

    stack_page = pmm_alloc_page();
    if (pmm_free_page(gap_page) != 0) {
        result = SV39_ERR_PAGE_FREE_FAILED;
        goto clean_up;
    }

    if (stack_page != NULL &&
        ((uintptr_t)stack_page == (uintptr_t)code_page ||
         (uintptr_t)stack_page + SV39_PAGE_SIZE == (uintptr_t)code_page ||
         (uintptr_t)code_page + SV39_PAGE_SIZE == (uintptr_t)stack_page)) {
        result = SV39_ERR_INVALID_ARGUMENT;
        goto clean_up;
    }
"""

SCENARIOS = [
    {
        "name": "baseline",
        "kind": "repo_as_is",
    },
    {
        "name": "length_zero",
        "kind": "generated",
        # bad address, length 0: must return 0 before checking address/root
        "setup": "    li a0, 0x40002000\n    li a1, 0\n",
        "expected_return": 0,
        "expected_bytes": b"",
    },
    {
        "name": "unmapped_start",
        "kind": "generated",
        "setup": "    li a0, 0x40002000\n    li a1, 1\n",
        "expected_return": -1,
        "expected_bytes": b"",
    },
    {
        "name": "length_256",
        "kind": "generated",
        "setup": """
    .Lpayload_address:
        auipc a0, %pcrel_hi(.Lpayload)
        addi a0, a0, %pcrel_lo(.Lpayload_address)
        li a1, 256
    """,
        "data": """
    .Lpayload:
        .fill 257, 1, 81
    """,
        "expected_return": 256,
        "expected_bytes": b"Q" * 256,
    },
    {
        "name": "length_257",
        "kind": "generated",
        "setup": """
    .Lpayload_address:
        auipc a0, %pcrel_hi(.Lpayload)
        addi a0, a0, %pcrel_lo(.Lpayload_address)
        li a1, 257
    """,
        "data": """
    .Lpayload:
        .fill 257, 1, 81
    """,
        "expected_return": -1,
        "expected_bytes": b"",
    },
    {
        "name": "binary_bytes",
        "kind": "generated",
        "setup": """
    .Lpayload_address:
        auipc a0, %pcrel_hi(.Lpayload)
        addi a0, a0, %pcrel_lo(.Lpayload_address)
        li a1, 4
    """,
        "data": """
    .Lpayload:
        .byte 65, 0, 66, 255
    """,
        "expected_return": 4,
        "expected_bytes": b"A\x00B\xff",
    },
    {
        "name": "noncanonical",
        "kind": "generated",
        "setup": "    li a0, 0x0000004000000000\n    li a1, 1\n",
        "expected_return": -1,
        "expected_bytes": b"",
    },
    {
        "name": "address_overflow",
        "kind": "generated",
        "setup": "    li a0, -2\n    li a1, 4\n",
        "expected_return": -1,
        "expected_bytes": b"",
    },
    {
        "name": "kernel_missing_u",
        "kind": "generated",
        "setup": "    li a0, 0x80200000\n    li a1, 1\n",
        "expected_return": -1,
        "expected_bytes": b"",
    },
    {
        "name": "cross_page_unmapped",
        "kind": "generated",
        "setup": """
        li t0, 0x40001ffe
        li t1, 80
        sb t1, 0(t0)
        li t1, 81
        sb t1, 1(t0)

        mv a0, t0
        li a1, 4
    """,
        "expected_return": -1,
        "expected_bytes": b"",
    },
    {
        "name": "execute_only",
        "kind": "generated",
        "main_replacements": [
            (
                "    const uint64_t user_code_flags =\n"
                "        SV39_PTE_R |\n",

                "    const uint64_t user_code_flags =\n",
            ),
        ],
        "setup": "    li a0, 0x40000000\n    li a1, 1\n",
        "expected_return": -1,
        "expected_bytes": b"",
    },
    {
        "name": "cross_page_valid",
        "kind": "generated",
        "main_replacements": [
            (
                "    stack_page = pmm_alloc_page();",
                SEPARATE_STACK_ALLOCATION,
            ),
        ],
        "setup": """
            li t0, 0x40001000
            li t1, 82
            sb t1, 0(t0)
            li t1, 83
            sb t1, 1(t0)

            li a0, 0x40000ffe
            li a1, 4
        """,
        "data": """
            .org 4094
            .byte 80, 81
        """,
        "expected_return": 4,
        "expected_bytes": b"PQRS",
    },
]


def prepare_main(workspace: Path, scenario: dict) -> None:
    source = (REPO / "kernel" / "main.c").read_text()

    for old, new in scenario.get("main_replacements", []):
        if source.count(old) != 1:
            raise SystemExit("main.c replacement anchor is not unique")
        source = source.replace(old, new, 1)

    (workspace / "kernel" / "main.c").write_text(source)

def main() -> None:
    requested = set(sys.argv[1:])
    unknown = requested - {scenario["name"] for scenario in SCENARIOS}
    if unknown:
        raise SystemExit(f"unknown scenario(s): {', '.join(sorted(unknown))}")
    scenarios = [
        scenario for scenario in SCENARIOS
        if not requested or scenario["name"] in requested
    ]
    if not scenarios:
        raise SystemExit("no matching scenario")

    failures = 0
    with tempfile.TemporaryDirectory(prefix="tinyos-write-") as temporary:
        workspace = Path(temporary)
        copy_project(workspace)
        for scenario in scenarios:
            print(f"=== {scenario['name']} ===")
            try:
                prepare_main(workspace, scenario)
                if scenario["kind"] == "repo_as_is":
                    run_baseline(workspace)
                else:
                    run_generated(workspace, scenario)
                print(f"[PASS] {scenario['name']}")
            except (SystemExit, OSError, subprocess.TimeoutExpired) as error:
                print(f"[FAIL] {scenario['name']}: {error}")
                failures += 1

    if failures != 0:
        raise SystemExit(f"{failures} scenario(s) failed")

if __name__ == "__main__":
    main()
