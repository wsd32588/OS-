#!/usr/bin/env python3
"""Verify the real task-creation failure branch in a temporary QEMU kernel."""

from pathlib import Path
import shutil
import subprocess
import tempfile


PROBE = r'''
static Sv39PageTable *g_probe_root;
static unsigned int g_probe_root_free_attempts;

int __real_pmm_free_page(void *page);

int __wrap_pmm_free_page(void *page) {
    if (page == g_probe_root) {
        ++g_probe_root_free_attempts;
    }
    return __real_pmm_free_page(page);
}

static void probe_fail(const char *message) {
    uart_puts("[task rollback] FAIL ");
    uart_puts(message);
    uart_putchar('\n');
    for (;;) {
        asm volatile("wfi");
    }
}

static int probe_memory_equal(const UserMemory *left, const UserMemory *right) {
    return left->root == right->root &&
        left->code_page == right->code_page &&
        left->stack_page == right->stack_page &&
        left->code_address == right->code_address &&
        left->stack_address == right->stack_address;
}

static void probe_fill_task_slots(void) {
    /* Fill through the actual API, without duplicating MAX_TASKS. */
    for (unsigned int added = 0; added < 16; ++added) {
        if (task_create(task_b) < 0) {
            if (added == 0) {
                probe_fail("no spare slot for the additional kernel task");
            }
            return;
        }
    }
    probe_fail("task capacity exceeds probe limit");
}

static void probe_check_prepared(
    Sv39PageTable *root, const UserMemory *memory, unsigned long initial_pages
) {
    uint64_t pa = 0;
    uint64_t flags = 0;
    const uint64_t code_flags = SV39_PTE_U | SV39_PTE_R | SV39_PTE_X | SV39_PTE_A;
    const uint64_t stack_flags = SV39_PTE_U | SV39_PTE_R | SV39_PTE_W |
        SV39_PTE_A | SV39_PTE_D;
    if (memory->root != root || memory->code_page == NULL ||
        memory->stack_page == NULL || memory->code_page == memory->stack_page ||
        memory->code_address != DEMO_USER_CODE_ADDRESS ||
        memory->stack_address != DEMO_USER_STACK_ADDRESS ||
        pmm_available_pages() + 4 != initial_pages ||
        sv39_query_page(root, memory->code_address, &pa, &flags) != SV39_OK ||
        pa != (uintptr_t)memory->code_page || flags != (code_flags | SV39_PTE_V) ||
        sv39_query_page(root, memory->stack_address, &pa, &flags) != SV39_OK ||
        pa != (uintptr_t)memory->stack_page || flags != (stack_flags | SV39_PTE_V)) {
        probe_fail("prepared descriptor/mappings/four-page allocation");
    }
}

static void probe_check_identity_range(
    Sv39PageTable *root, uintptr_t start, uintptr_t end, uint64_t flags
) {
    for (uintptr_t address = start; address < end; address += SV39_PAGE_SIZE) {
        if (!identity_mapping_is_present(root, address, flags)) {
            probe_fail("kernel identity mapping changed");
        }
    }
}

static void probe_check_cleanup(
    int result, Sv39PageTable *root, const UserMemory *memory,
    unsigned long initial_pages, uint64_t original_satp
) {
    const UserMemory empty = {0};
    uint64_t pa = 0;
    uint64_t flags = 0;
    if (result != SV39_OK || !probe_memory_equal(memory, &empty) ||
        pmm_available_pages() != initial_pages ||
        sv39_query_page(root, DEMO_USER_CODE_ADDRESS, &pa, &flags) != SV39_ERR_NOT_MAPPED ||
        sv39_query_page(root, DEMO_USER_STACK_ADDRESS, &pa, &flags) != SV39_ERR_NOT_MAPPED ||
        root->entries[sv39_vpn_index(DEMO_USER_CODE_ADDRESS, 2)] != 0 ||
        g_probe_root_free_attempts != 0 || sv39_read_satp() != original_satp) {
        probe_fail("caller cleanup result/pages/descriptor/root");
    }
    const uint64_t text_flags = SV39_PTE_R | SV39_PTE_X | SV39_PTE_A;
    const uint64_t rodata_flags = SV39_PTE_R | SV39_PTE_A;
    const uint64_t writable_flags = SV39_PTE_R | SV39_PTE_W | SV39_PTE_A | SV39_PTE_D;
    probe_check_identity_range(root, (uintptr_t)__text_start, (uintptr_t)__text_end,
                               text_flags);
    probe_check_identity_range(root, (uintptr_t)__rodata_start, (uintptr_t)__rodata_end,
                               rodata_flags);
    probe_check_identity_range(root, (uintptr_t)__data_start, (uintptr_t)__data_end,
                               writable_flags);
    if (!identity_mapping_is_present(root, (uintptr_t)root, writable_flags) ||
        !identity_mapping_is_present(root, UINT64_C(0x10000000), writable_flags)) {
        probe_fail("root/UART identity mapping changed");
    }
    uart_puts("[task rollback] PASS four pages restored; descriptor empty; root preserved\n");
}
'''


def replace_once(source: str, anchor: str, replacement: str) -> str:
    if source.count(anchor) != 1:
        raise SystemExit(f"Cannot locate unique kernel probe anchor: {anchor!r}")
    return source.replace(anchor, replacement, 1)


def instrument(source: str) -> str:
    definition = "void kernel_main(unsigned long hart_id, const void* device_tree) {"
    source = replace_once(source, definition, PROBE + "\n" + definition)
    preparation = "    if (prepare_demo_user_memory("
    source = replace_once(source, preparation, """    g_probe_root = kernel_root;
    const unsigned long probe_initial_pages = pmm_available_pages();
""" + preparation)
    creation = "    int user_task_id = task_create_user("
    source = replace_once(source, creation, """    probe_fill_task_slots();
    probe_check_prepared(kernel_root, &user_memory, probe_initial_pages);
    const UserMemory probe_original_memory = user_memory;
""" + creation)
    failure = "    if (task_a_id < 0 ||"
    source = replace_once(source, failure, """    if (task_a_id < 0 || task_b_id < 0 || user_task_id != -1 ||
        !probe_memory_equal(&user_memory, &probe_original_memory)) {
        probe_fail("actual user creation must fail and preserve caller descriptor");
    }
    uart_puts("[task rollback] PASS full slots reject user task; caller descriptor preserved\\n");

""" + failure)
    cleanup = "        int clean_result = user_memory_release(&user_memory);"
    source = replace_once(source, cleanup, cleanup + """
        probe_check_cleanup(clean_result, kernel_root, &user_memory,
                            probe_initial_pages, satp_value);
""")
    return source


def run() -> None:
    repository = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="tinyos-task-rollback-") as temporary:
        workspace = Path(temporary)
        for name in ("include", "kernel", "drivers"):
            shutil.copytree(repository / name, workspace / name,
                            ignore=shutil.ignore_patterns("*.o", "*.d"))
        for name in ("Makefile", "linker.ld"):
            shutil.copy2(repository / name, workspace / name)
        main = workspace / "kernel/main.c"
        main.write_text(instrument(main.read_text()))

        linker = "riscv64-linux-gnu-ld --wrap=pmm_free_page"
        build = subprocess.run(["make", f"LD={linker}"], cwd=workspace,
                               capture_output=True, text=True, timeout=60)
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
            if "[task rollback]" in line or "Task creation failed." in line:
                print(line)
        required = (
            "[task rollback] PASS full slots reject user task; caller descriptor preserved",
            "Task creation failed.",
            "[task rollback] PASS four pages restored; descriptor empty; root preserved",
        )
        if (any(marker not in output for marker in required) or
                "[FAIL]" in output or "[task rollback] FAIL" in output or
                "[user fault]" in output or "Starting preemptive scheduler" in output):
            print(output[-3000:])
            raise SystemExit("Task-creation failure or caller rollback verification failed.")
        print("[PASS] task-slot exhaustion and actual kernel_main caller rollback")


if __name__ == "__main__":
    try:
        run()
    except (OSError, subprocess.TimeoutExpired) as error:
        raise SystemExit(f"Task rollback verification could not run: {error}") from error
