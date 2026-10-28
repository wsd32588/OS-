#!/usr/bin/env python3
"""Dictionary-driven user data-page checks in temporary QEMU kernels.

Fill SCENARIOS with user C sources and expected observations. The production
kernel is copied, never rewritten. Existing WRITE helpers handle build and
bounded QEMU execution, including termination on timeout.
"""

from pathlib import Path
import subprocess
import sys
import tempfile

from verify_syscall_write import build, copy_project, run_qemu


RELEASE_PASS = b"[data probe] PASS five pages restored; descriptor empty; root preserved"
FAULT_MARKER = b"[user fault]"

# This wrapper checks the actual release used by both EXIT and user faults.
# Kernel tasks have empty UserMemory and do not produce a data-test marker.
RELEASE_PROBE = r'''
#include "mm/user_memory.h"
#include <stddef.h>
#include "mm/pmm.h"
#include "drivers/uart.h"
#include "arch/riscv/vm_arch.h"

static Sv39PageTable *g_data_probe_root;
static unsigned int g_data_probe_root_free_attempts;

int __real_pmm_free_page(void *page);
int __real_user_memory_release(UserMemory *memory);

static void data_probe_fail(const char *message) {
    uart_puts("[data probe] FAIL ");
    uart_puts(message);
    uart_putchar('\n');
    for (;;) {
        asm volatile("wfi");
    }
}

int __wrap_pmm_free_page(void *page) {
    if (g_data_probe_root != NULL && page == g_data_probe_root) {
        ++g_data_probe_root_free_attempts;
        return -1;
    }
    return __real_pmm_free_page(page);
}

int __wrap_user_memory_release(UserMemory *memory) {
    if (memory == NULL || memory->root == NULL) {
        return __real_user_memory_release(memory);
    }

    const UserMemory original = *memory;
    const unsigned long before = pmm_available_pages();
    const uint64_t original_satp = sv39_read_satp();
    const uintptr_t addresses[3] = {
        original.code_address, original.stack_address, original.data_address
    };
    void *const pages[3] = {
        original.code_page, original.stack_page, original.data_page
    };
    const uint64_t expected_flags[3] = {
        SV39_PTE_V | SV39_PTE_U | SV39_PTE_R | SV39_PTE_X | SV39_PTE_A,
        SV39_PTE_V | SV39_PTE_U | SV39_PTE_R | SV39_PTE_W | SV39_PTE_A | SV39_PTE_D,
        SV39_PTE_V | SV39_PTE_U | SV39_PTE_R | SV39_PTE_W | SV39_PTE_A | SV39_PTE_D
    };
    uint64_t pa = 0;
    uint64_t flags = 0;
    if (addresses[0] != UINT64_C(0x40000000) ||
        addresses[1] != UINT64_C(0x40001000) ||
        addresses[2] != UINT64_C(0x40003000) ||
        pages[0] == NULL || pages[1] == NULL || pages[2] == NULL ||
        pages[0] == pages[1] || pages[0] == pages[2] || pages[1] == pages[2]) {
        data_probe_fail("prepared descriptor");
    }
    for (unsigned int i = 0; i < 3; ++i) {
        if (sv39_query_page(original.root, addresses[i], &pa, &flags) != SV39_OK ||
            pa != (uintptr_t)pages[i] || flags != expected_flags[i]) {
            data_probe_fail("prepared PA/permissions");
        }
    }
    if (sv39_query_page(original.root, UINT64_C(0x40002000), &pa, &flags) !=
        SV39_ERR_NOT_MAPPED) {
        data_probe_fail("stack-top hole must remain unmapped");
    }

    g_data_probe_root = original.root;
    g_data_probe_root_free_attempts = 0;
    const int result = __real_user_memory_release(memory);
    if (result != SV39_OK || g_data_probe_root_free_attempts != 0 ||
        pmm_available_pages() != before + 5 ||
        memory->root != NULL || memory->code_page != NULL ||
        memory->stack_page != NULL || memory->data_page != NULL ||
        memory->code_address != 0 || memory->stack_address != 0 ||
        memory->data_address != 0 || sv39_read_satp() != original_satp) {
        data_probe_fail("release result/pages/descriptor/root/satp");
    }
    for (unsigned int i = 0; i < 3; ++i) {
        if (sv39_query_page(original.root, addresses[i], &pa, &flags) !=
            SV39_ERR_NOT_MAPPED) {
            data_probe_fail("released mapping remains");
        }
    }
    if (original.root->entries[sv39_vpn_index(addresses[0], 2)] != 0 ||
        sv39_query_page(original.root, (uintptr_t)original.root, &pa, &flags) !=
            SV39_OK || pa != (uintptr_t)original.root) {
        data_probe_fail("empty branch/root identity mapping");
    }
    g_data_probe_root = NULL;
    uart_puts("[data probe] PASS five pages restored; descriptor empty; root preserved\n");
    return result;
}
'''


SCENARIOS = [
    {
        "name": "baseline",
        "user_source": None,  # None: run the real user/main.c unchanged.
        "image_kind": "data",  # data: 0x3000..0x4000; short: 1..4096.
        "expected_output": (b"Hello from user!\n", b"Data OK\n"),
        "expected_fault": None,
    },
    {
        "name": "pure_bss",
        "user_source": r"""
#include "user_api.h"
#include <stddef.h>

static volatile unsigned char buffer[32];

int main(void) {
    for (size_t i = 0; i < 32; ++i) {
        if (buffer[i] != 0) {
            user_write("[write] failed\n", 15);
            user_exit();
        }
        buffer[i] = (unsigned char)(i + 1);
    }

    if (user_sleep(2) != 0) {
        user_write("[write] failed\n", 15);
        user_exit();
    }

    for (size_t i = 0; i < 32; ++i) {
        if (buffer[i] != (unsigned char)(i + 1)) {
            user_write("[write] failed\n", 15);
            user_exit();
        }
    }

    long ret = user_write("BSS OK\n", 7);
    if (ret != 7) {
        user_write("[write] failed\n", 15);
        user_exit();
    }

    return 0;
}
""",
        "image_kind": "short",
        "expected_output": (b"BSS OK\n",),
        "expected_fault": None,
    },
    {
        "name": "data_execute",
        "user_source": r"""
#include "user_api.h"
#include <stdint.h>

int main(void) {
    void (*func)(void) = (void (*)(void))(uintptr_t)0x40003000;
    func();

    user_write("UNREACHABLE\n", 12);
    return 0;
}
""",
        "image_kind": "short",
        "expected_output": (),
        "expected_fault": {"scause": 12, "sepc": 0x40003000, "stval": 0x40003000},
        "forbidden_output": (b"UNREACHABLE\n",),
    },
    {
        "name": "code_write",
        "user_source": r"""
#include "user_api.h"
#include <stdint.h>

int main(void) {
    volatile unsigned char *code_ptr = (volatile unsigned char *)(uintptr_t)0x40000000;
    *code_ptr = 0x90;

    user_write("UNREACHABLE\n", 12);
    return 0;
}
""",
        "image_kind": "short",
        "expected_output": (),
        "expected_fault": {"scause": 15, "stval": 0x40000000},
        "forbidden_output": (b"UNREACHABLE\n",),
    },
]


def validate_scenario(scenario: dict) -> None:
    source = scenario["user_source"]
    if source is not None and (not isinstance(source, str) or not source.strip()):
        raise ValueError(f"{scenario['name']}: user_source still needs a C program")
    if scenario["image_kind"] not in ("data", "short"):
        raise ValueError("image_kind must be data or short")
    for marker in (*scenario["expected_output"], *scenario.get("forbidden_output", ())):
        if not isinstance(marker, bytes) or not marker:
            raise ValueError("output markers must be nonempty bytes")
    fault = scenario["expected_fault"]
    if fault is not None:
        if not isinstance(fault, dict) or "scause" not in fault:
            raise ValueError("expected_fault must contain scause")
        for register, value in fault.items():
            if register not in ("scause", "sepc", "stval") or not isinstance(value, int):
                raise ValueError("fault entries must be scause/sepc/stval integer pairs")
            if not 0 <= value <= (1 << 64) - 1:
                raise ValueError("fault register value is outside uint64 range")


def check_output(output: bytes, scenario: dict) -> None:
    for marker in (b"[write] failed", b"[data probe] FAIL", b"[user memory] release failed",
                   b"kernel halt", b"[FAIL]", *scenario.get("forbidden_output", ())):
        if marker in output:
            raise ValueError(f"unexpected output {marker!r}")
    for marker in scenario["expected_output"]:
        if output.count(marker) != 1:
            raise ValueError(f"expected exactly one {marker!r}")

    fault = scenario["expected_fault"]
    if fault is None:
        if FAULT_MARKER in output or output.count(b"[syscall] exit") != 1:
            raise ValueError("normal scenario must reach EXIT without a user fault")
        finish_marker = b"[syscall] exit"
    else:
        if output.count(FAULT_MARKER) != 1 or b"[syscall] exit" in output:
            raise ValueError("fault scenario must terminate through exactly one user fault")
        finish_marker = FAULT_MARKER
        fault_output = output.partition(FAULT_MARKER)[2].partition(RELEASE_PASS)[0]
        for register, value in fault.items():
            expected = f"{register} = 0x{value:016x}".encode()
            if expected not in fault_output:
                raise ValueError(f"missing fault register {expected!r}")

    if output.count(RELEASE_PASS) != 1 or RELEASE_PASS not in output.partition(finish_marker)[2]:
        raise ValueError("missing or misplaced five-page release check")
    after_release = output.partition(RELEASE_PASS)[2]
    if after_release.count(b"[timer] tick in S-mode") < 2 or b"AA" not in after_release:
        raise ValueError("A/timer did not continue after release")
    if b"[timer] tick in U-mode" in after_release:
        raise ValueError("terminated user task ran again")


def run_scenario(workspace: Path, scenario: dict) -> None:
    if scenario["user_source"] is not None:
        (workspace / "user/main.c").write_text(scenario["user_source"], encoding="utf-8")
    (workspace / "kernel/mm/user_data_probe.c").write_text(RELEASE_PROBE, encoding="utf-8")
    makefile = workspace / "Makefile"
    source = makefile.read_text()
    anchor = "all: kernel.elf"
    if source.count(anchor) != 1:
        raise ValueError("cannot find unique Makefile all target")
    source = source.replace(anchor, "OBJS += kernel/mm/user_data_probe.o\n\n" + anchor, 1)
    anchor = "$(LD) -T linker.ld $(OBJS) -o $@"
    if source.count(anchor) != 1:
        raise ValueError("cannot find unique kernel link command")
    source = source.replace(anchor, "$(LD) --wrap=user_memory_release --wrap=pmm_free_page "
                            "-T linker.ld $(OBJS) -o $@", 1)
    makefile.write_text(source)
    build(workspace)

    image_size = (workspace / "user/build/demo.bin").stat().st_size
    minimum, maximum = (0x3000, 0x4000) if scenario["image_kind"] == "data" else (1, 4096)
    if not minimum <= image_size <= maximum:
        raise ValueError(f"image size {image_size} is outside {scenario['image_kind']} range")
    output = run_qemu(workspace)
    try:
        check_output(output, scenario)
    except ValueError:
        sys.stderr.buffer.write(output[-6000:])
        sys.stderr.buffer.flush()
        raise


def main() -> None:
    scenarios = {scenario["name"]: scenario for scenario in SCENARIOS}
    if len(scenarios) != len(SCENARIOS):
        raise ValueError("duplicate scenario names")
    names = sys.argv[1:]
    if names == ["--list"]:
        for name, scenario in scenarios.items():
            state = "pending" if scenario["user_source"] == "" else "ready"
            print(f"{name}: {state}")
        return
    if not names:
        names = ["baseline"]
    elif names == ["--all"]:
        names = list(scenarios)
    for name in names:
        if name not in scenarios:
            raise ValueError(f"unknown scenario: {name}")
        validate_scenario(scenarios[name])
    for name in names:
        print(f"=== {name} ===", flush=True)
        with tempfile.TemporaryDirectory(prefix="tinyos-user-data-") as temporary:
            workspace = Path(temporary)
            copy_project(workspace)
            run_scenario(workspace, scenarios[name])
        print(f"[PASS] {name}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        raise SystemExit(f"[FAIL] user data verification: {error}") from error
