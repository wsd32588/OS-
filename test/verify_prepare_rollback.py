#!/usr/bin/env python3
"""Exercise the actual user-memory setup in a temporary QEMU kernel."""

from pathlib import Path
import shutil
import subprocess
import tempfile


PROBE = r'''
static unsigned int g_probe_alloc_failure;
static unsigned int g_probe_alloc_calls;
static unsigned int g_probe_flush_failure;
static unsigned int g_probe_flush_calls;

void *__real_pmm_alloc_page(void);
int __real_sv39_flush_page(uint64_t virtual_address);

void *__wrap_pmm_alloc_page(void) {
    if (g_probe_alloc_failure != 0 &&
        ++g_probe_alloc_calls == g_probe_alloc_failure) {
        return NULL;
    }
    return __real_pmm_alloc_page();
}

int __wrap_sv39_flush_page(uint64_t virtual_address) {
    if (g_probe_flush_failure != 0 &&
        ++g_probe_flush_calls == g_probe_flush_failure) {
        return SV39_ERR_INVALID_ARGUMENT;
    }
    return __real_sv39_flush_page(virtual_address);
}

static int probe_memory_equal(const UserMemory *left, const UserMemory *right) {
    return left->root == right->root &&
        left->code_page == right->code_page &&
        left->stack_page == right->stack_page &&
        left->code_address == right->code_address &&
        left->stack_address == right->stack_address;
}

static int probe_check_result(
    int actual, int expected, unsigned long before,
    const UserMemory *output, const UserMemory *original
) {
    if (actual != expected || pmm_available_pages() != before ||
        !probe_memory_equal(output, original)) {
        uart_puts("[rollback] FAIL result/pages/output: ");
        uart_put_hex((unsigned long)actual);
        uart_putchar('/');
        uart_put_hex(pmm_available_pages());
        uart_putchar('/');
        uart_put_hex(before);
        uart_putchar('\n');
        return -1;
    }
    return 0;
}

static int probe_check_missing(Sv39PageTable *root, uint64_t address) {
    uint64_t physical_address = 0;
    uint64_t flags = 0;
    if (sv39_query_page(root, address, &physical_address, &flags) !=
        SV39_ERR_NOT_MAPPED) {
        uart_puts("[rollback] FAIL unexpected user mapping\n");
        return -1;
    }
    return 0;
}

static int probe_prepare_rollback(Sv39PageTable *root) {
    /* Sentinel fields expose any writes to output on a failed call. */
    const UserMemory original = {
        .root = root, .code_page = NULL, .stack_page = NULL,
        .code_address = 1, .stack_address = 2
    };
    uint64_t physical_address = 0;
    uint64_t flags = 0;
    const unsigned long initial_pages = pmm_available_pages();
    unsigned int root_index = sv39_vpn_index(DEMO_USER_CODE_ADDRESS, 2);
    if (root->entries[root_index] != 0) {
        uart_puts("[rollback] FAIL user branch must initially be absent\n");
        return -1;
    }

    /* Code, stack, L1, then L0 are the four actual allocations. */
    for (unsigned int failure = 1; failure <= 4; ++failure) {
        UserMemory output = original;
        g_probe_alloc_calls = 0;
        g_probe_alloc_failure = failure;
        int result = prepare_demo_user_memory(root, &output);
        g_probe_alloc_failure = 0;
        if (g_probe_alloc_calls < failure ||
            probe_check_result(result, SV39_ERR_NO_MEMORY, initial_pages,
                               &output, &original) != 0 ||
            probe_check_missing(root, DEMO_USER_CODE_ADDRESS) != 0 ||
            probe_check_missing(root, DEMO_USER_STACK_ADDRESS) != 0 ||
            root->entries[root_index] != 0) {
            return -1;
        }
        uart_puts("[rollback] PASS allocation failure ");
        uart_put_hex(failure);
        uart_putchar('\n');
    }

    for (unsigned int failure = 1; failure <= 2; ++failure) {
        UserMemory output = original;
        g_probe_flush_calls = 0;
        g_probe_flush_failure = failure;
        int result = prepare_demo_user_memory(root, &output);
        g_probe_flush_failure = 0;
        if (g_probe_flush_calls < failure ||
            probe_check_result(result, SV39_ERR_INVALID_ARGUMENT, initial_pages,
                               &output, &original) != 0 ||
            probe_check_missing(root, DEMO_USER_CODE_ADDRESS) != 0 ||
            probe_check_missing(root, DEMO_USER_STACK_ADDRESS) != 0 ||
            root->entries[root_index] != 0) {
            return -1;
        }
        uart_puts("[rollback] PASS flush failure ");
        uart_put_hex(failure);
        uart_putchar('\n');
    }

    const uint64_t addresses[2] = {
        DEMO_USER_CODE_ADDRESS, DEMO_USER_STACK_ADDRESS
    };
    const uint64_t foreign_flags = SV39_PTE_U | SV39_PTE_R | SV39_PTE_A;
    for (unsigned int conflict = 0; conflict < 2; ++conflict) {
        void *foreign_page = pmm_alloc_page();
        if (foreign_page == NULL ||
            sv39_map_page(root, addresses[conflict],
                          (uint64_t)(uintptr_t)foreign_page, foreign_flags) != SV39_OK ||
            sv39_flush_page(addresses[conflict]) != SV39_OK) {
            uart_puts("[rollback] FAIL conflict setup\n");
            return -1;
        }
        unsigned long before = pmm_available_pages();
        UserMemory output = original;
        int result = prepare_demo_user_memory(root, &output);
        if (probe_check_result(result, SV39_ERR_ALREADY_MAPPED, before,
                               &output, &original) != 0 ||
            sv39_query_page(root, addresses[conflict],
                            &physical_address, &flags) != SV39_OK ||
            physical_address != (uint64_t)(uintptr_t)foreign_page ||
            flags != (foreign_flags | SV39_PTE_V) ||
            probe_check_missing(root, addresses[1 - conflict]) != 0) {
            uart_puts("[rollback] FAIL foreign mapping changed\n");
            return -1;
        }
        if (sv39_unmap_page(root, addresses[conflict]) != SV39_OK ||
            sv39_flush_page(addresses[conflict]) != SV39_OK ||
            pmm_free_page(foreign_page) != 0 ||
            sv39_reclaim_empty_tables(root, addresses[conflict]) != SV39_OK ||
            pmm_available_pages() != initial_pages ||
            root->entries[root_index] != 0) {
            uart_puts("[rollback] FAIL conflict teardown\n");
            return -1;
        }
        uart_puts("[rollback] PASS mapping conflict ");
        uart_put_hex(conflict);
        uart_putchar('\n');
    }
    uart_puts("[rollback] PASS all 8 failure cases\n");
    return 0;
}
'''


def run() -> None:
    repository = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="tinyos-prepare-rollback-") as temporary:
        workspace = Path(temporary)
        for name in ("include", "kernel", "drivers"):
            shutil.copytree(repository / name, workspace / name,
                            ignore=shutil.ignore_patterns("*.o", "*.d"))
        for name in ("Makefile", "linker.ld"):
            shutil.copy2(repository / name, workspace / name)

        main = workspace / "kernel/main.c"
        source = main.read_text()
        definition = "void kernel_main(unsigned long hart_id, const void* device_tree) {"
        call = "    if (prepare_demo_user_memory("
        if source.count(definition) != 1 or source.count(call) != 1:
            raise SystemExit("Cannot locate the current kernel setup; update probe anchors.")
        source = source.replace(definition, PROBE + "\n" + definition)
        source = source.replace(call, """    if (probe_prepare_rollback(kernel_root) != 0) {
        uart_puts("[rollback] FAIL suite stopped\\n");
        for (;;) {
            asm volatile("wfi");
        }
    }

""" + call)
        main.write_text(source)

        linker = "riscv64-linux-gnu-ld --wrap=pmm_alloc_page --wrap=sv39_flush_page"
        build = subprocess.run(["make", f"LD={linker}"], cwd=workspace,
                               capture_output=True, text=True)
        if build.returncode != 0:
            raise SystemExit(build.stdout + build.stderr)

        process = subprocess.Popen(
            ["qemu-system-riscv64", "-machine", "virt", "-m", "128M",
             "-nographic", "-bios", "default", "-kernel", "kernel.elf"],
            cwd=workspace, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            output, _ = process.communicate(timeout=8)
        except subprocess.TimeoutExpired:
            process.terminate()
            output, _ = process.communicate(timeout=5)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()

        for line in output.splitlines():
            if "[rollback]" in line or "[syscall] exit" in line:
                print(line)
        success = "[rollback] PASS all 8 failure cases"
        after_exit = output.partition("[syscall] exit")[2]
        if (success not in output or "[rollback] FAIL" in output or
                "setup failed" in output or "release failed" in output or
                "[user fault]" in output or after_exit.count("[timer] tick in S-mode") < 2):
            print(output[-3000:])
            raise SystemExit("Rollback or subsequent normal scheduling failed.")
        print("[PASS] rollback suite and normal exit followed by timer scheduling")


if __name__ == "__main__":
    run()
