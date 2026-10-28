#ifndef TINYOS_USER_IMAGE_H
#define TINYOS_USER_IMAGE_H

#include <stddef.h>

enum {
    USER_IMAGE_OK = 0,
    USER_IMAGE_INVALID_ARGUMENT = -1
};
#ifdef __cplusplus
    extern "C"{
#endif
int user_image_load_segment (
    void* destination,              //已经可访问的目标缓冲区起点
    size_t destination_capacity,    //目标实际可用字节数
    const void* image,              //完整镜像缓冲区起点
    size_t image_size,              //镜像实际可用字节数
    size_t file_offset,             //本段有初值内容在镜像中的起始偏移
    size_t file_size,               //需要从文件复制的字节数
    size_t memory_size              //本段在内存中总共占用的字节数
);
#ifdef __cplusplus
}
#endif

#endif