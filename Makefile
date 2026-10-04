CC = riscv64-linux-gnu-gcc
LD = riscv64-linux-gnu-ld
QEMU = qemu-system-riscv64
HOST_CC ?= cc
HOST_CXX ?= c++
CMAKE ?= cmake
CTEST ?= ctest
TEST_BUILD_DIR ?= test/build/cmake

ARCH_FLAGS = -march=rv64imac_zicsr_zifencei \
             -mabi=lp64 \
             -mcmodel=medany \
             -mno-relax

CFLAGS = -Wall -Wextra -O0 -g \
         -ffreestanding \
         -fno-stack-protector \
         -fno-pie \
         -Iinclude \
         $(ARCH_FLAGS)

DEPFLAGS = -MMD -MP

OBJS = \
    kernel/entry.o \
    kernel/main.o \
    kernel/pmm.o \
    kernel/trap_entry.o \
    kernel/trap.o \
    kernel/sbi.o \
    kernel/timer.o \
    kernel/sched.o \
	kernel/vm.o \
	kernel/vm_arch.o \
	kernel/user_entry.o \
	kernel/syscall.o \
	kernel/user_memory.o \
    drivers/uart.o

DEPS = $(OBJS:.o=.d)

.PHONY: all clean compdb debug qemu sync test test-qemu

all: kernel.elf

%.o: %.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

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

compdb: test
	bear -- $(MAKE) -B all
	python3 test/merge_compile_commands.py compile_commands.json "$(TEST_BUILD_DIR)/compile_commands.json"

clean:
	rm -f kernel/*.o kernel/*.d drivers/*.o drivers/*.d kernel.elf \
		*.o *.d test/tinyos_tests
	if [ -f "$(TEST_BUILD_DIR)/CMakeCache.txt" ]; then \
		$(CMAKE) --build "$(TEST_BUILD_DIR)" --target clean; \
	fi

-include $(DEPS)

sync:
	@mkdir -p /mnt/d/PROJECTS/C/tinyos
	rsync -av --exclude='*.o' --exclude='*.d' --exclude='*.elf' --exclude='.git' ~/tinyos/ /mnt/d/PROJECTS/C/tinyos/
	@echo "[+] Synced to Windows D: drive successfully!"
