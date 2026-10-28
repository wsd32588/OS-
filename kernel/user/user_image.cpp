#include "user/user_image.h"

extern "C" {
int user_image_load_segment(
    void *destination,
    size_t destination_capacity,
    const void *image,
    size_t image_size,
    size_t file_offset,
    size_t file_size,
    size_t memory_size
) {
    if(
        destination == NULL &&
        image == NULL &&
        destination_capacity == 0 &&
        image_size == 0 &&
        file_size == 0 &&
        file_offset == 0 &&
        memory_size == 0
    ) {
        return 0;
    } else if (
        (memory_size != 0 && destination == NULL) ||
        (image_size != 0 && image == NULL) ||
        file_size > memory_size ||
        memory_size > destination_capacity ||
        file_offset > image_size ||
        file_size > image_size - file_offset
    ) {
        return USER_IMAGE_INVALID_ARGUMENT;
    }

    unsigned char* output = (unsigned char*)destination;
    const unsigned char* input = (const unsigned char*)image;

    for (size_t i = 0; i < file_size;++i) {
        output[i] = input[file_offset + i];
    }

    for (size_t i = file_size; i < memory_size; ++i) {
        output[i] = 0;
    }

    return USER_IMAGE_OK;
}
}