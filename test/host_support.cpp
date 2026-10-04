#include "host_support.hpp"

#include <cstdio>
#include <ostream>

namespace {
tinyos_test::CallCounts g_calls;
std::vector<tinyos_test::CallEvent> g_events;
void* g_free_failure_page = nullptr;
unsigned long g_flush_failure_countdown = 0;
}

extern "C" int __real_sv39_unmap_page(Sv39PageTable *root, uint64_t address);
extern "C" int __real_pmm_free_page(void *page);

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

UserMemoryState capture_user_memory(const UserMemory &memory) {
    const MemoryDescriptor descriptor{memory.root, memory.code_page, memory.stack_page,
                                      memory.code_address, memory.stack_address};
    UserMemoryState state{descriptor, pmm_available_pages(), g_calls, {}, {}};
    if (memory.root != nullptr) {
        state.code_mapping = query_mapping(memory.root, memory.code_address);
        state.stack_mapping = query_mapping(memory.root, memory.stack_address);
    }
    return state;
}

std::ostream &operator<<(std::ostream &stream, const MemoryDescriptor &memory) {
    return stream << "{root=" << memory.root << ", code=" << memory.code_page
                  << ", stack=" << memory.stack_page << ", code_va=" << memory.code_address
                  << ", stack_va=" << memory.stack_address << '}';
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
    return stream << "{memory=" << state.memory << ", pages=" << state.available_pages
                  << ", calls=" << state.calls << ", code_mapping=" << state.code_mapping
                  << ", stack_mapping=" << state.stack_mapping << '}';
}

} // namespace tinyos_test
