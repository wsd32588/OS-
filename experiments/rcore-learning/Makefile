.DEFAULT_GOAL := help

TARGET := riscv64gc-unknown-none-elf
KERNEL_ELF := os/target/$(TARGET)/release/rcore-learning-os
KERNEL_BIN := $(KERNEL_ELF).bin
USER_ELF := user/target/$(TARGET)/release/hello
USER_BIN := $(USER_ELF).bin
PEER_ELF := user/target/$(TARGET)/release/hello_peer
PEER_BIN := $(PEER_ELF).bin
IMAGE_ELF := user/target/paged/$(TARGET)/release/image_demo
IMAGE_INPUT := $(IMAGE_ELF).elf
SYSCALL_ELF := user/target/paged/$(TARGET)/release/syscall_demo
SYSCALL_INPUT := $(SYSCALL_ELF).elf
TASK_ELF := user/target/paged/$(TARGET)/release/task_demo
TASK_INPUT := $(TASK_ELF).elf
PREEMPT_ELF := user/target/paged/$(TARGET)/release/preempt_demo
PREEMPT_INPUT := $(PREEMPT_ELF).elf
BOOTLOADER := .cache/reference/bootloader/rustsbi-qemu.bin
# Use the verified system build even when an interactive shell prepends a dev tree.
QEMU ?= /usr/bin/qemu-system-riscv64
OBJCOPY ?= riscv64-linux-gnu-objcopy
READELF ?= riscv64-linux-gnu-readelf
OBJDUMP ?= riscv64-linux-gnu-objdump

.PHONY: help build run inspect disasm user user-inspect user-disasm user-elf user-elf-inspect user-syscall-elf user-task-elf user-preempt-elf
help:
	@echo 'build: compile author code; run: boot with pinned RustSBI'
	@echo 'inspect: show ELF layout; disasm: show instructions'
	@echo 'user: build chapter 2 application; user-inspect/user-disasm: inspect it'
	@echo 'user-elf/user-elf-inspect: build/inspect the separate paged ELF image'
	@echo 'user-syscall-elf: build the paged write/resume ELF fixture'
	@echo 'user-task-elf: build the independent paged cooperative-task fixture'
	@echo 'user-preempt-elf: build the independent paged preemption fixture'
	@echo 'BATCH22.md: preempt two paged users with real supervisor timers.'

build: user user-elf user-syscall-elf user-task-elf user-preempt-elf
	cd os && bash ../scripts/cargo.sh build --release --offline --locked
	$(OBJCOPY) -O binary $(KERNEL_ELF) $(KERNEL_BIN)

run: build
	@echo '31f5d013f81421ed9fd33208dfa1c3796f9d72bd11c0f939566581ac2f3e97d3  $(BOOTLOADER)' | sha256sum --check --status
	$(QEMU) -machine virt -m 128M -smp 1 -nographic -bios $(BOOTLOADER) -device loader,file=$(KERNEL_BIN),addr=0x80200000

inspect: build
	$(READELF) -hW -lW -sW $(KERNEL_ELF)

disasm: build
	$(OBJDUMP) -d $(KERNEL_ELF)

user:
	cd user && bash ../scripts/cargo.sh build --bin hello --release --offline --locked
	$(OBJCOPY) -O binary $(USER_ELF) $(USER_BIN)
	cd user && bash ../scripts/cargo.sh rustc --bin hello_peer --release --offline --locked -- -C link-arg=--defsym=USER_BASE_ADDRESS=0x80420000
	$(OBJCOPY) -O binary $(PEER_ELF) $(PEER_BIN)

user-inspect: user
	$(READELF) -hW -lW -sW $(USER_ELF)
	$(READELF) -hW -lW -sW $(PEER_ELF)

user-disasm: user
	$(OBJDUMP) -d $(USER_ELF)
	$(OBJDUMP) -d $(PEER_ELF)

# Separate flags and target directory preserve the old applications' layout.
user-elf:
	cd user && RUSTFLAGS='-C link-arg=-Tsrc/paged.ld -C force-frame-pointers=yes' bash ../scripts/cargo.sh build --bin image_demo --target-dir target/paged --release --offline --locked
	$(OBJCOPY) --strip-debug $(IMAGE_ELF) $(IMAGE_INPUT)

user-elf-inspect: user-elf
	$(READELF) -hW -lW $(IMAGE_INPUT)

user-syscall-elf:
	cd user && RUSTFLAGS='-C link-arg=-Tsrc/paged.ld -C force-frame-pointers=yes' bash ../scripts/cargo.sh build --bin syscall_demo --target-dir target/paged --release --offline --locked
	$(OBJCOPY) --strip-debug $(SYSCALL_ELF) $(SYSCALL_INPUT)

user-task-elf:
	cd user && RUSTFLAGS='-C link-arg=-Tsrc/paged.ld -C force-frame-pointers=yes' bash ../scripts/cargo.sh build --bin task_demo --target-dir target/paged --release --offline --locked
	$(OBJCOPY) --strip-debug $(TASK_ELF) $(TASK_INPUT)

user-preempt-elf:
	cd user && RUSTFLAGS='-C link-arg=-Tsrc/paged.ld -C force-frame-pointers=yes' bash ../scripts/cargo.sh build --bin preempt_demo --target-dir target/paged --release --offline --locked
	$(OBJCOPY) --strip-debug $(PREEMPT_ELF) $(PREEMPT_INPUT)
