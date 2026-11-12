CC = riscv64-linux-gnu-gcc
CXX = clang++
LD = riscv64-linux-gnu-ld
OBJCOPY = riscv64-linux-gnu-objcopy
QEMU = qemu-system-riscv64
HOST_CC ?= cc
HOST_CXX ?= c++
CMAKE ?= cmake
CTEST ?= ctest
TEST_BUILD_DIR ?= test/build/cmake
SYNC_DEST ?= /mnt/d/PROJECTS/C/tinyos
OPTIMIZATION ?= -O0
TASK_STRESS_ROUNDS ?= 500

ARCH_FLAGS = -march=rv64imac_zicsr_zifencei \
             -mabi=lp64 \
             -mcmodel=medany \
             -mno-relax

CFLAGS = -Wall -Wextra $(OPTIMIZATION) -g \
         -ffreestanding \
         -fno-stack-protector \
         -fno-pie \
         -Iinclude \
         $(ARCH_FLAGS)

CXXFLAGS = --target=riscv64-unknown-elf \
           -std=c++20 -Wall -Wextra -Wpedantic $(OPTIMIZATION) -g \
           -ffreestanding \
           -fno-exceptions -fno-rtti -fno-threadsafe-statics \
           -fno-stack-protector -fno-pie \
           -fno-unwind-tables -fno-asynchronous-unwind-tables \
           -Iinclude \
		   -Ifreestanding \
           $(ARCH_FLAGS)

DEPFLAGS = -MMD -MP

USER_BUILD_DIR = user/build

USER_COMMON_OBJS = \
    $(USER_BUILD_DIR)/start.o \
    $(USER_BUILD_DIR)/syscall.o
USER_DEMO_OBJS = $(USER_COMMON_OBJS) \
    $(USER_BUILD_DIR)/main.o
USER_WORKER_OBJS = $(USER_COMMON_OBJS) \
    $(USER_BUILD_DIR)/worker.o
USER_OBJS = $(sort $(USER_DEMO_OBJS) $(USER_WORKER_OBJS))

USER_CFLAGS = $(ARCH_FLAGS) -std=c11 -Wall -Wextra $(OPTIMIZATION) -g \
    -ffreestanding -fno-builtin -fno-stack-protector -fno-pie \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -msmall-data-limit=0 -Iinclude -Iuser

OBJS = \
    kernel/boot/entry.o \
    kernel/boot/main.o \
    kernel/boot/private/kernel_vm.o \
    kernel/boot/private/demo_tasks.o \
    kernel/boot/private/boot_stages.o \
    kernel/mm/pmm.o \
	kernel/mm/user_loader.o \
    kernel/arch/riscv/trap_entry.o \
    kernel/arch/riscv/task_context.o \
    kernel/task/trap.o \
    kernel/arch/riscv/sbi.o \
    kernel/arch/riscv/panic.o \
    kernel/task/timer.o \
    kernel/task/sched.o \
	kernel/task/task.o \
	kernel/task/task_table.o \
    kernel/task/user_task_create.o \
    kernel/task/user_task_exec.o \
    kernel/task/user_task_exit.o \
    kernel/mm/vm.o \
    kernel/arch/riscv/vm_arch.o \
	kernel/runtime/cpp_runtime.o \
    kernel/user/user_entry.o \
    kernel/task/syscall.o \
    kernel/mm/user_memory.o \
    kernel/mm/user_access.o \
	kernel/mm/user_address_space.o \
	kernel/process/process.o \
	kernel/process/spawn.o \
	kernel/process/builtin_program.o \
    kernel/user/user_image.o \
    kernel/user/user_elf.o \
    lib/string.o \
    drivers/uart.o

DEPS = $(OBJS:.o=.d) $(USER_OBJS:.o=.d)

.PHONY: all user clean compdb debug qemu sync sync-dry-run test test-qemu test-stress

all: kernel.elf

user: $(USER_BUILD_DIR)/demo.bin $(USER_BUILD_DIR)/worker.elf

$(USER_BUILD_DIR)/demo.elf: $(USER_DEMO_OBJS) user/user.ld
	$(LD) --no-relax -T user/user.ld $(USER_DEMO_OBJS) -o $@

$(USER_BUILD_DIR)/worker.elf: $(USER_WORKER_OBJS) user/user.ld
	$(LD) --no-relax -T user/user.ld $(USER_WORKER_OBJS) -o $@

kernel/user/user_entry.o: $(USER_BUILD_DIR)/demo.bin \
    $(USER_BUILD_DIR)/demo.elf $(USER_BUILD_DIR)/worker.elf

$(USER_BUILD_DIR):
	mkdir -p $@

$(USER_BUILD_DIR)/%.o: user/%.c | $(USER_BUILD_DIR)
	$(CC) $(USER_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(USER_BUILD_DIR)/%.o: user/%.S | $(USER_BUILD_DIR)
	$(CC) $(USER_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(USER_BUILD_DIR)/demo.bin: $(USER_BUILD_DIR)/demo.elf
	$(OBJCOPY) -O binary $< $@

kernel/user/user_entry.o: $(USER_BUILD_DIR)/demo.bin

%.o: %.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

kernel.elf: $(OBJS) linker.ld
	$(LD) -T linker.ld $(OBJS) -o $@

qemu: kernel.elf
	$(QEMU) \
		-machine virt \
		-m 128M \
		-nographic \
		-bios default \
		-kernel kernel.elf

debug: kernel.elf
	$(QEMU) \
		-machine virt \
		-m 128M \
		-nographic \
		-bios default \
		-kernel kernel.elf \
		-S \
		-gdb tcp:127.0.0.1:2600

test:
	$(CMAKE) -S test -B "$(TEST_BUILD_DIR)" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_C_COMPILER="$(HOST_CC)" -DCMAKE_CXX_COMPILER="$(HOST_CXX)"
	$(CMAKE) --build "$(TEST_BUILD_DIR)"
	$(CTEST) --test-dir "$(TEST_BUILD_DIR)" --output-on-failure

test-qemu:
	python3 test/verify_prepare_rollback.py
	python3 test/verify_task_creation_rollback.py

test-stress: test
	python3 test/verify_task_table_stress.py --rounds "$(TASK_STRESS_ROUNDS)"

compdb: test
	bear -- $(MAKE) -B all
	python3 test/merge_compile_commands.py compile_commands.json "$(TEST_BUILD_DIR)/compile_commands.json"

clean:
	rm -f kernel/*.o kernel/*.d drivers/*.o drivers/*.d kernel.elf \
		kernel/*/*.o kernel/*/*.d kernel/*/*/*.o kernel/*/*/*.d \
		kernel/arch/riscv/*.o kernel/arch/riscv/*.d \
		lib/*.o lib/*.d \
		*.o *.d test/tinyos_tests $(USER_OBJS) $(USER_OBJS:.o=.d) \
		$(USER_BUILD_DIR)/demo.elf $(USER_BUILD_DIR)/demo.bin $(USER_BUILD_DIR)/worker.elf
	if [ -f "$(TEST_BUILD_DIR)/CMakeCache.txt" ]; then \
		$(CMAKE) --build "$(TEST_BUILD_DIR)" --target clean; \
	fi

-include $(DEPS)

# Keep .gitignore patterns compatible with rsync --exclude-from (no ! rules).
SYNC_EXCLUDES = --exclude='.git' --exclude-from="$(CURDIR)/.gitignore"

sync:
	@mkdir -p "$(SYNC_DEST)"
	rsync -av $(SYNC_EXCLUDES) "$(CURDIR)/" "$(SYNC_DEST)/"
	@echo "[+] Synced source files to $(SYNC_DEST)"

sync-dry-run:
	rsync -av --dry-run --stats $(SYNC_EXCLUDES) "$(CURDIR)/" "$(SYNC_DEST)/"

# Optional local analysis: does not participate in kernel/test builds.
ARCHITECTURE_SCOPE ?= kernel
ARCHITECTURE_PYTHON ?= .cache/architecture/venv/bin/python
.PHONY: architecture-install architecture architecture-libclang architecture-uml architecture-joern architecture-test
architecture-install:
	python3 tools/architecture/install.py

architecture: architecture-libclang architecture-uml architecture-joern

architecture-libclang:
	$(ARCHITECTURE_PYTHON) tools/architecture/analyze.py libclang --scope $(ARCHITECTURE_SCOPE)

architecture-uml:
	$(ARCHITECTURE_PYTHON) tools/architecture/analyze.py uml --scope $(ARCHITECTURE_SCOPE)

architecture-joern:
	$(ARCHITECTURE_PYTHON) tools/architecture/analyze.py joern --scope $(ARCHITECTURE_SCOPE)

architecture-test:
	$(ARCHITECTURE_PYTHON) -B tools/architecture/test_analyze.py
	$(ARCHITECTURE_PYTHON) -B tools/architecture/test_export.py

.PHONY: architecture-export
architecture-export:
	$(ARCHITECTURE_PYTHON) -B tools/architecture/export.py --scope $(ARCHITECTURE_SCOPE)
