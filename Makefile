CC = riscv64-linux-gnu-gcc
LD = riscv64-linux-gnu-ld
QEMU = qemu-system-riscv64
HOST_CC ?= cc

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

HOST_TEST_CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -g \
                   -Iinclude -Itest

TEST_BIN = test/tinyos_tests

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
    drivers/uart.o

DEPS = $(OBJS:.o=.d)

.PHONY: all clean compdb debug qemu sync test

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

test: $(TEST_BIN)
	./$(TEST_BIN)

compdb:
	bear -- sh -c '$(MAKE) -B && $(MAKE) -B test'

$(TEST_BIN): test/test_main.c test/test_pmm.c test/test_pmm.h \
			 test/test_vm.c test/test_vm.h \
             kernel/pmm.c include/pmm.h \
			 kernel/vm.c include/vm.h
	$(HOST_CC) $(HOST_TEST_CFLAGS) \
		test/test_main.c test/test_pmm.c \
		test/test_vm.c \
		kernel/vm.c kernel/pmm.c\
		-o $@

clean:
	rm -f kernel/*.o kernel/*.d drivers/*.o drivers/*.d kernel.elf \
		*.o *.d $(TEST_BIN)

-include $(DEPS)

sync:
	@mkdir -p /mnt/d/PROJECTS/C/tinyos
	rsync -av --exclude='*.o' --exclude='*.d' --exclude='*.elf' --exclude='.git' ~/tinyos/ /mnt/d/PROJECTS/C/tinyos/
	@echo "[+] Synced to Windows D: drive successfully!"
