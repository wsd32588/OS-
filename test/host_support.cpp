#include "host_support.hpp"

#include <cstdio>
#include <ostream>

namespace {
tinyos_test::CallCounts g_calls;
std::vector<tinyos_test::CallEvent> g_events;
void* g_free_failure_page = nullptr;
unsigned long g_flush_failure_countdown = 0;
unsigned long g_allocation_calls = 0;
uint64_t g_satp = 0;
}

extern "C" int __real_sv39_unmap_page(Sv39PageTable *root, uint64_t address);
extern "C" int __real_pmm_free_page(void *page);
extern "C" void* __real_pmm_alloc_page(void);

extern "C" void* __wrap_pmm_alloc_page(void) {
    ++g_allocation_calls;
    return __real_pmm_alloc_page();
}

extern "C" uint64_t sv39_read_satp(void) {
    return g_satp;
}

extern "C" int __wrap_sv39_unmap_page(Sv39PageTable *root, uint64_t address) {
    ++g_calls.unmap;
    const int result = __real_sv39_unmap_page(root, address);
    g_events.push_back({tinyos_test::CallKind::Unmap, address, result});
    return result;
}

extern "C" int __wrap_pmm_free_page(void *page) {
    ++g_calls.free;
    const int result =
        g_free_failure_page != nullptr && page == g_free_failure_page
        ? -1 : __real_pmm_free_page(page);

    g_events.push_back({tinyos_test::CallKind::Free,
                        reinterpret_cast<uintptr_t>(page), result});
    return result;
}

extern "C" int sv39_flush_page(uint64_t address) {
    ++g_calls.flush;
    int result = sv39_virtual_address_is_canonical(address) &&
        (address & (SV39_PAGE_SIZE - 1)) == 0 ? SV39_OK : SV39_ERR_INVALID_ARGUMENT;
    if (g_flush_failure_countdown != 0 && --g_flush_failure_countdown == 0) {
        result = SV39_ERR_INVALID_ARGUMENT;
    }
    g_events.push_back({tinyos_test::CallKind::Flush, address, result});
    return result;
}

extern "C" void uart_puts(const char *text) {
    std::fputs(text, stderr);
}

namespace tinyos_test {

unsigned long host_allocation_calls() {
    return g_allocation_calls;
}

void host_set_satp(uint64_t value) {
    g_satp = value;
}

void host_set_free_failure(void *page) {
    g_free_failure_page = page;
}

void host_set_flush_failure(unsigned long attempt) {
    g_flush_failure_countdown = attempt;
}

const CallCounts &host_call_counts() {
    return g_calls;
}

const std::vector<CallEvent> &host_call_events() {
    return g_events;
}

Mapping query_mapping(const Sv39PageTable *root, uint64_t address) {
    Mapping mapping;
    mapping.result = sv39_query_page(root, address,
        &mapping.physical_address, &mapping.flags);
    return mapping;
}

MemoryDescriptor describe(const UserMemory &memory) {
    MemoryDescriptor result{};
    result.root = memory.root;
    result.page_count = memory.page_count;
    // Snapshot the entire bounded descriptor, including any unused tail.
    for (std::size_t i = 0; i < result.pages.size(); ++i) {
        result.pages[i] = {memory.pages[i].virtual_address,
                           memory.pages[i].physical_page};
    }
    return result;
}

UserMemoryState capture_user_memory(const UserMemory &memory) {
    UserMemoryState state{describe(memory), pmm_available_pages(), g_calls, {}};
    const std::size_t count = memory.page_count < USER_MEMORY_MAX_PAGE
        ? memory.page_count : USER_MEMORY_MAX_PAGE;
    if (memory.root != nullptr) {
        for (std::size_t i = 0; i < count; ++i) {
            state.mappings[i] = query_mapping(memory.root,
                                              memory.pages[i].virtual_address);
        }
    }
    return state;
}

std::ostream &operator<<(std::ostream &stream, const MemoryDescriptor &memory) {
    stream << "{root=" << memory.root << ", count=" << memory.page_count
           << ", pages=[";
    for (std::size_t i = 0; i < memory.pages.size(); ++i) {
        if (i != 0) {
            stream << ", ";
        }
        stream << "{va=" << memory.pages[i].virtual_address
               << ", pa=" << memory.pages[i].physical_page << '}';
    }
    return stream << "]}";
}

std::ostream &operator<<(std::ostream &stream, const CallCounts &calls) {
    return stream << "{unmap=" << calls.unmap << ", flush=" << calls.flush
                  << ", free=" << calls.free << '}';
}

std::ostream &operator<<(std::ostream &stream, const Mapping &mapping) {
    return stream << "{result=" << mapping.result << ", pa=" << mapping.physical_address
                  << ", flags=" << mapping.flags << '}';
}

std::ostream &operator<<(std::ostream &stream, const UserMemoryState &state) {
    stream << "{memory=" << state.memory << ", pages=" << state.available_pages
           << ", calls=" << state.calls << ", mappings=[";
    for (std::size_t i = 0; i < state.mappings.size(); ++i) {
        if (i != 0) {
            stream << ", ";
        }
        stream << state.mappings[i];
    }
    return stream << "]}";
}

} // namespace tinyos_test
