#ifndef TINYOS_TEST_HOST_SUPPORT_HPP
#define TINYOS_TEST_HOST_SUPPORT_HPP

#include "c_api.hpp"

#include <cstdint>
#include <iosfwd>
#include <vector>

namespace tinyos_test {

enum class CallKind { Unmap, Flush, Free };

struct CallCounts {
    unsigned long unmap = 0;
    unsigned long flush = 0;
    unsigned long free = 0;
    bool operator==(const CallCounts &) const = default;
};

struct CallEvent {
    CallKind kind;
    uintptr_t address;
    int result;
};

const CallCounts &host_call_counts();
const std::vector<CallEvent> &host_call_events();

struct MemoryDescriptor {
    Sv39PageTable *root;
    void *code_page;
    void *stack_page;
    uintptr_t code_address;
    uintptr_t stack_address;
    bool operator==(const MemoryDescriptor &) const = default;
};

struct Mapping {
    int result = SV39_ERR_NOT_MAPPED;
    uint64_t physical_address = 0;
    uint64_t flags = 0;
    bool operator==(const Mapping &) const = default;
};

struct UserMemoryState {
    MemoryDescriptor memory;
    unsigned long available_pages;
    CallCounts calls;
    Mapping code_mapping;
    Mapping stack_mapping;
    bool operator==(const UserMemoryState &) const = default;
};

Mapping query_mapping(const Sv39PageTable *root, uint64_t address);
UserMemoryState capture_user_memory(const UserMemory &memory);
std::ostream &operator<<(std::ostream &stream, const MemoryDescriptor &memory);
std::ostream &operator<<(std::ostream &stream, const CallCounts &calls);
std::ostream &operator<<(std::ostream &stream, const Mapping &mapping);
std::ostream &operator<<(std::ostream &stream, const UserMemoryState &state);

//非空指针时指定失败的页:nullptr 关闭注入
void host_set_free_failure(void* page);

// Fail the specified upcoming flush once; zero disables the injection.
void host_set_flush_failure(unsigned long attempt);

} // namespace tinyos_test

#endif
