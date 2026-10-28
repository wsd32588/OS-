HOST_CC ?= cc
CXX ?= g++
AR ?= ar
RUSTC ?= rustc
ROOT := ../..
BUILD := build
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -g -I$(ROOT)/include
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Werror -O2 -g
RUSTFLAGS := --edition=2021 -D warnings

.PHONY: all test check-freestanding check-riscv

all: test

test: $(BUILD)/cpp_tests $(BUILD)/rust_tests
	./$(BUILD)/cpp_tests
	./$(BUILD)/rust_tests --test-threads=1

$(BUILD):
	mkdir -p $@

$(BUILD)/reference_pmm.o: $(ROOT)/kernel/pmm.c $(ROOT)/include/pmm.h | $(BUILD)
	$(HOST_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/reference_vm.o: $(ROOT)/kernel/vm.c $(ROOT)/include/vm.h $(ROOT)/include/pmm.h | $(BUILD)
	$(HOST_CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libtinyos_reference.a: $(BUILD)/reference_pmm.o $(BUILD)/reference_vm.o
	$(AR) rcs $@ $^

$(BUILD)/cpp_tests: tests/cpp_tests.cpp tests/reference.h cpp/pmm.cpp cpp/pmm.hpp cpp/sv39.hpp $(BUILD)/libtinyos_reference.a
	$(CXX) $(CXXFLAGS) cpp/pmm.cpp tests/cpp_tests.cpp $(BUILD)/libtinyos_reference.a -o $@

$(BUILD)/libtinyos_trial.rlib: rust/lib.rs rust/pmm.rs rust/sv39.rs | $(BUILD)
	$(RUSTC) $(RUSTFLAGS) --crate-name tinyos_trial --crate-type rlib rust/lib.rs -o $@

$(BUILD)/rust_tests: tests/rust_tests.rs $(BUILD)/libtinyos_trial.rlib $(BUILD)/libtinyos_reference.a
	$(RUSTC) $(RUSTFLAGS) --test tests/rust_tests.rs --extern tinyos_trial=$(BUILD)/libtinyos_trial.rlib -L native=$(BUILD) -l static=tinyos_reference -o $@

# These checks produce objects/libraries; they do not link a bootable kernel.
check-freestanding: $(BUILD)/libtinyos_trial.rlib | $(BUILD)
	$(CXX) $(CXXFLAGS) -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -c cpp/pmm.cpp -o $(BUILD)/cpp_pmm_freestanding.o
	$(CXX) $(CXXFLAGS) -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -c cpp/compile_probe.cpp -o $(BUILD)/cpp_sv39_freestanding.o

check-riscv: | $(BUILD)
	$(RUSTC) $(RUSTFLAGS) --crate-name tinyos_trial --crate-type rlib --target riscv64gc-unknown-none-elf -C panic=abort rust/lib.rs -o $(BUILD)/libtinyos_trial_riscv.rlib
