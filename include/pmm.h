#ifndef TINYOS_PMM_H
#define TINYOS_PMM_H

/* 物理页大小：4 KiB，所有分配地址都按这个粒度对齐。 */
#define PAGE_SIZE 4096UL

/* 初始化 [memory_start, memory_end) 为可分配物理区；成功返回 0。 */
int pmm_init(const void* memory_start, const void* memory_end);
/* 分配一个 4 KiB 页；成功返回页首地址，失败返回 NULL。 */
void* pmm_alloc_page(void);
/* 当前可分配页数 = 顺序区域剩余 + 空闲链表里的可复用页。 */
unsigned long pmm_available_pages(void);
/* 释放一个页；成功返回 0，非法地址或重复释放返回 -1。 */
int pmm_free_page(void* page);

#endif