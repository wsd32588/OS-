#include "pmm.h"

#include <stdint.h>
#include <stddef.h>

/*
 * PMM 全局状态。
 * 当前单 hart、无并发分配，所以这些静态变量不需要锁；
 * 以后多核或可抢占场景要重新设计。
 */
/* 下一个从未分配过的顺序页地址（页对齐）。 */
static unsigned long next_free_page;
/* 可分配物理区下界，pmm_free_page 用它做范围检查。 */
static unsigned long allocatable_start;
/* 可分配物理区上界（不含），顺序分配到此为止。 */
static unsigned long allocatable_end;

/*
 * 向上取整到 PAGE_SIZE 的整数倍。
 * 例：4097 -> 8192，4096 -> 4096。
 */
static unsigned long align_up(unsigned long value) {
    /* 先加 PAGE_SIZE-1 让零头进位，再与 ~(PAGE_SIZE-1) 清掉低 12 位。 */
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

/*
 * 向下取整到 PAGE_SIZE 的整数倍。
 * 例：4095 -> 0，4096 -> 4096，8191 -> 4096。
 */
static unsigned long align_down(unsigned long value) {
    /* 直接清掉低 12 位。 */
    return value & ~(PAGE_SIZE - 1);
}

/*
 * 空闲页链表节点。
 *
 * intrusive free list：不额外分配元数据，
 * 而是把 next 指针直接写在“空闲页”自己的开头。
 * 因此页一旦被释放，原来的内容就失效；
 * 分配出去之前必须重新清零（见 pmm_alloc_page）。
 *
 * 页都是 4 KiB 对齐，页首放得下一个 8 字节指针。
 */
struct free_page {
    struct free_page* next;
};

/* 空闲链表头；NULL 表示没有可复用页。 */
static struct free_page* free_list;
/* 空闲链表中的页数；pmm_available_pages 的一部分。 */
static unsigned long recycled_page_count;

/*
 * 初始化 PMM。
 *
 * memory_start / memory_end 是左闭右开区间 [start, end)：
 *   - start 向上对齐到页边界；
 *   - end 向下对齐到页边界；
 *   - 只管理完全落在区间内的整页。
 *
 * 当前 end 用 DTB 地址，避免把 DTB 本身分配出去；
 * 这是 QEMU virt + 固定 128 MiB 下的保守做法，不是通用 FDT 解析。
 */
int pmm_init(const void* memory_start, const void* memory_end) {
    /* const void* -> 整数；uintptr_t 与指针同宽，方便做位运算。 */
    uintptr_t raw_start = (uintptr_t)memory_start;
    uintptr_t raw_end = (uintptr_t)memory_end;

    /* 起点向上取整：丢掉 start 之前不完整的页。 */
    uintptr_t page_start = align_up(raw_start);
    /* 终点向下取整：丢掉 end 之后不完整的页。 */
    uintptr_t page_end = align_down(raw_end);

    /* 至少要有完整的一页，否则视为无效区间。 */
    if (page_start >= page_end) {
        return -1;
    }

    /* 顺序分配游标：从这里开始往后切新页。 */
    next_free_page = (unsigned long)page_start;
    /* 记录下界，供释放时做范围检查。 */
    allocatable_start = (unsigned long)page_start;
    /* 记录上界（不含）。 */
    allocatable_end = (unsigned long)page_end;

    /* 每次 init 都重置空闲链表和计数，旧的分配记录全部作废。 */
    free_list = NULL;
    recycled_page_count = 0;

    return 0;
}

/*
 * 分配一个 4 KiB 物理页，返回页起始地址（页对齐），失败返回 NULL。
 *
 * 分配顺序：
 *   1. 优先复用 free_list 里的页；
 *   2. free_list 为空才从 next_free_page 顺序向后切一块。
 * 无论走哪条路径，返回前都会把整页清零。
 */
void* pmm_alloc_page(void) {
    /* 有释放过的页：优先复用。 */
    if (free_list != NULL) {
    /* free_list 的节点就存放在空闲页的页首。 */
        struct free_page* page = free_list;
    /* 从链表头摘下一页。 */
        free_list = page->next;
    /* 这页不再算“可复用”，计数减一。 */
        --recycled_page_count;

    /* 以 8 字节为单位清零整页；必须在读完 page->next 之后再清零。 */
        uint64_t* p = (uint64_t*)page;
        for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i) {
            p[i] = 0;
        }

    /* 返回页首地址。 */
        return (void*)page;
    }
    /* 没有可复用页：从顺序区域切一块。 */
    else {
    /* 游标到达上界，说明物理内存耗尽。 */
        if (next_free_page >= allocatable_end) {
            return NULL;
        }

    /* 当前游标就是新页的页首。 */
        unsigned long allocated = next_free_page;
    /* 游标前进一页，保证下一轮不会重复分配。 */
        next_free_page += PAGE_SIZE;

    /* 新页同样要清零。 */
        uint64_t* p = (uint64_t*)allocated;
        for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i) {
            p[i] = 0;
        }

    /* 返回顺序分配到的页（属于上面的 else 分支）。 */
    return (void*)allocated;
    }
}

/*
 * 返回当前可分配页数。
 *
 * 两个来源相加：
 *   recycled_page_count         : free_list 里已释放、可复用的页；
 *   (end - next_free_page)/4KiB : 还没被顺序分配过的页。
 * 两者互不重叠：顺序游标只前进，释放页都在游标之前。
 */
unsigned long pmm_available_pages(void) {
    return recycled_page_count +
           (allocatable_end - next_free_page) / PAGE_SIZE;
}

/*
 * 释放一个之前由 pmm_alloc_page 返回的页。
 *
 * 校验：
 *   - page 不能是 NULL；
 *   - 必须 4 KiB 对齐；
 *   - 必须落在 [allocatable_start, next_free_page) 内；
 *   - 不能已经在 free_list 里（线性扫描检测重复释放）。
 *
 * 当前没有并发保护，也没有位图/引用计数；
 * 只适合单 hart、调用者自己保证配对使用。
 */
int pmm_free_page(void* page) {
    /* 指针 -> 整数，方便做对齐和范围检查。 */
    uintptr_t addr = (uintptr_t)page;
    /* NULL 不是合法页地址。 */
    if (addr == 0 ||
    /* 必须 4 KiB 对齐，否则它不是一个页首。 */
        (addr & (PAGE_SIZE - 1)) != 0 ||
    /* 低于可分配下界，不属于 PMM 管理范围。 */
        addr < allocatable_start ||
    /* >= next_free_page：这页还没被顺序分配过，不能释放。 */
        addr >= next_free_page) {
        return -1;
    }

    /* 线性扫描 free_list，检测重复释放。 */
    for (struct free_page *current = free_list;
        current != NULL;
        current = current->next) {
        if (current == page) {
            return -1;
        }
    }

    /* 空闲页的页首现在当作 free_page 节点使用。 */
    struct free_page* free_page = (struct free_page*)page;

    /* 头插：新节点的 next 指向原来的链表头。 */
    free_page->next = free_list;
    /* 更新链表头。 */
    free_list = free_page;
    /* 可复用页数加一。 */
    ++recycled_page_count;


    return 0;
}
