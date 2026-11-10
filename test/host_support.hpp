#ifndef TINYOS_TEST_HOST_SUPPORT_HPP
#define TINYOS_TEST_HOST_SUPPORT_HPP

#include "c_api.hpp"

#include <array>
#include <cstddef>
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
unsigned long host_allocation_calls();
void host_set_satp(uint64_t value);

struct PageDescriptor {
    uintptr_t virtual_address = 0;
    void* physical_page = nullptr;
    bool operator==(const PageDescriptor &) const = default;
};

struct MemoryDescriptor {
    Sv39PageTable* root = nullptr;
    std::size_t page_count = 0;
    std::array<PageDescriptor, USER_MEMORY_MAX_PAGE> pages{};
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
    std::array<Mapping, USER_MEMORY_MAX_PAGE> mappings;
    bool operator==(const UserMemoryState &) const = default;
};

Mapping query_mapping(const Sv39PageTable *root, uint64_t address);
MemoryDescriptor describe(const UserMemory &memory);
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
