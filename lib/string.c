#include <stddef.h>

void* memcpy(void* destination, const void* source, size_t length) {
    unsigned char* output = destination;
    const unsigned char* input = source;

    for (size_t i = 0; i < length; ++i) {
        output[i] = input[i];
    }
    return destination;
}

void* memset(void* destination, int value, size_t length) {
    unsigned char* output = destination;
    for (size_t i = 0; i < length; ++i) {
        output[i] = (unsigned char)value;
    }
    return destination;
}