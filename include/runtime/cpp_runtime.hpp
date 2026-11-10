#pragma once
extern "C" {
int cpp_runtime_init(void);
int __cxa_atexit(void (*function)(void*), void* object, void* dso);
void __cxa_finalize(void* dso);
}
