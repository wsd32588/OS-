#ifndef TINYOS_USER_ELF_H
#define TINYOS_USER_ELF_H

#include <stddef.h>
#include <stdint.h>

#define USER_ELF_MAX_LOAD_SEGMENTS 2U

enum {
    USER_ELF_OK = 0,
    USER_ELF_INVALID_ARGUMENT = -1
};

typedef struct {
    uint64_t file_offset;
    uint64_t virtual_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
    uint32_t flags;
} UserElfSegment; //44 bytes

typedef struct {
    uint64_t entry_address;
    uint64_t program_header_count;
    size_t load_segment_count;
    UserElfSegment segments[USER_ELF_MAX_LOAD_SEGMENTS];
} UserElfLayout; //考虑到对齐是120 bytes

int user_elf_parse (const void* image, size_t image_size, UserElfLayout* layout);

#endif