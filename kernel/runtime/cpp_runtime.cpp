#include "runtime/cpp_runtime.hpp"

#include <stddef.h>

using InitFunction = void(*)(void);
//using FiniFunction = void(*)(void);

using AtexitDestructor = void (*)(void*);

extern "C" {
    extern InitFunction __init_array_start[];
    extern InitFunction __init_array_end[];

    void* __dso_handle = nullptr;
}

/*
 *function：调用哪个析构入口
 * object：把哪个对象的地址传进去
 * dso：这条记录属于哪个模块
 */
struct AtexitEntry {
    void (*function)(void*);
    void* object;
    void* dso;
};

namespace {
    constexpr size_t MAX_ATEXIT_ENTRIES = 64;
    AtexitEntry s_atexit_entries[MAX_ATEXIT_ENTRIES];
    size_t s_atexit_count = 0;
    bool _is_full = false;
    bool s_init_started = false;
}

int __cxa_atexit(void (*function)(void*), void* object, void* dso) {
    if (s_atexit_count >= MAX_ATEXIT_ENTRIES) {
        _is_full = true; // Set the flag to indicate that the atexit entries are full
        return -1; // Return an error code if the maximum number of entries is exceeded
    }
    if (function == nullptr) {
        return -1; // Return an error code if the function pointer is null
    }

    s_atexit_entries[s_atexit_count] = AtexitEntry{
        .function = function,
        .object = object,
        .dso = dso
    };
    s_atexit_count++;
    // This is a stub implementation. In a real implementation, you would store the function and object
    // in a list to be called during finalization.
    return 0; // Return 0 on success
}

void __cxa_finalize(void* dso) {

    for (size_t i = s_atexit_count; i > 0; --i) {
        AtexitEntry& entry = s_atexit_entries[i - 1];

        if (entry.function == nullptr ||
            (dso != nullptr && entry.dso != dso)) {
            continue; // Skip if the function is null or the dso does not match
        }

        const AtexitEntry temp_entry = entry; // Store the entry to call after removing it
        entry.function = nullptr; // Mark the entry as called
        temp_entry.function(temp_entry.object); // Call the function with the object
    }
    // This is a stub implementation. In a real implementation, you would call the registered functions
    // for the given dso.
    return; // Return nothing on success
}

int cpp_runtime_init(void) {
    if (s_init_started) {
        return -1;
    }

    s_init_started = true;
    for (InitFunction* func = __init_array_start; func < __init_array_end; ++func) {
        if (*func != nullptr) {
            (*func)(); // Call each initialization function
        }
    }

    if (_is_full) {
        return -1; // Return an error code if the atexit entries are full
    }
    return 0; // Return 0 on success
}
