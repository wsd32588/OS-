#ifndef TINYOS_VM_H
#define TINYOS_VM_H

#include <stdint.h>

/*
 * Sv39 页表纯逻辑接口，不直接操作 CSR。
 *
 * Sv39 PTE 是 64 位：
 *   [9:0]   flags（V/R/W/X/U/G/A/D/RSW）
 *   [53:10] PPN[2:0]，44 位物理页号
 *   [63:54] 预留/扩展
 *
 * Sv39 虚拟地址只有低 39 位有效：
 *   [38:30] VPN[2]   [29:21] VPN[1]   [20:12] VPN[0]   [11:0] 页内偏移
 *
 * 一张页表恰好占 4 KiB，可放 512 个 8 字节 PTE，
 * 所以每个 VPN 字段是 9 位。
 */
/* 4 KiB 页：页内偏移占低 12 位。 */
#define SV39_PAGE_SHIFT         12U
/* 页大小：4096 字节。 */
#define SV39_PAGE_SIZE          (1UL << SV39_PAGE_SHIFT)
/* 每级 VPN 字段占 9 位。 */
#define SV39_VPN_BITS           9U
/* Sv39 共三级页表（VPN[2] -> VPN[1] -> VPN[0]）。 */
#define SV39_LEVEL_COUNT        3U
/* 一张页表中的 PTE 数量：2^9 = 512。 */
#define SV39_PTE_COUNT          (1UL << SV39_VPN_BITS)
/* PTE 中 PPN 的起始位：低 10 位留给 flags。 */
#define SV39_PTE_PPN_SHIFT           10U
/* VPN 级别越界时返回的非法标记。 */
#define SV39_INVALID_INDEX      SV39_PTE_COUNT

/*
 * PTE 低 10 位标志：
 *   V 有效、R 可读、W 可写、X 可执行、U 用户可访问、
 *   G 全局映射、A 已访问、D 已写。
 */
#define SV39_PTE_V       UINT64_C(1)
#define SV39_PTE_R      (UINT64_C(1) << 1)
#define SV39_PTE_W      (UINT64_C(1) << 2)
#define SV39_PTE_X      (UINT64_C(1) << 3)
#define SV39_PTE_U      (UINT64_C(1) << 4)
#define SV39_PTE_G      (UINT64_C(1) << 5)
#define SV39_PTE_A      (UINT64_C(1) << 6)
#define SV39_PTE_D      (UINT64_C(1) << 7)

/* 叶子 PTE 允许出现的权限位（不含 V 和 RSW）；sv39_map_page 用它拒绝非法 flags。 */
#define SV39_PTE_LEAF_FLAG_MASK \
    (SV39_PTE_R | SV39_PTE_W | SV39_PTE_X | SV39_PTE_U | \
     SV39_PTE_G | SV39_PTE_A | SV39_PTE_D)

/* 低 8 位（V..D）的标志掩码；RSW（bit 8..9）不在其中。 */
#define SV39_PTE_FLAG_MASK UINT64_C(0xff)

/*
 *错误码
*/
/* 成功。 */
#define SV39_OK                      0
/* 参数非法：空指针、非 canonical、未对齐、flags 非法等。 */
#define SV39_ERR_INVALID_ARGUMENT   -1
/* 分配中间级页表时 PMM 没有可用页。 */
#define SV39_ERR_NO_MEMORY          -2
/* 目标 VA 已经有映射。 */
#define SV39_ERR_ALREADY_MAPPED     -3
/* 走到 1GB/2MB 大页叶 PTE，无法继续拆成 4KB。 */
#define SV39_ERR_INTERMEDIATE_LEAF  -4
/* 查询时中间 PTE 无效，或叶子不存在。 */
#define SV39_ERR_NOT_MAPPED         -5
/* 页表中出现非法 PTE（例如 R=0、W=1）。 */
#define SV39_ERR_INVALID_PTE        -6

/* 64 位 PTE：低 10 位 flags，中间 44 位 PPN。 */
typedef uint64_t Sv39Pte;

/* 大小恰好为 4 KiB 的 Sv39 页表；使用时要保证实例按 4 KiB 对齐。 */
typedef struct {
    Sv39Pte entries[SV39_PTE_COUNT];
}Sv39PageTable;

/* 从 PMM 分配一张清零的根页表；失败返回 NULL。 */
/* 创建一个Sv39 页表 */
Sv39PageTable* sv39_create_page_table(void);

/* 建立 4 KiB 页映射 va -> pa；成功返回 SV39_OK，失败返回负错误码。 */
/* 建立/修改页映射 */
int sv39_map_page(
    Sv39PageTable* root,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
);

/* 查询 4 KiB 页映射；physical_address_out 返回“页基址 | 页内偏移”。 */
/* 查询/模拟地址翻译 */
int sv39_query_page(
    const Sv39PageTable* root,
    uint64_t virtual_address,
    uint64_t* physical_address_out,
    uint64_t* flags_out
);

/* 编译期保证页表大小恰好是一页。 */
_Static_assert(
    sizeof(Sv39PageTable) == SV39_PAGE_SIZE,
    "an SV39 page table must occupy exactly one page"
);

/*
 * 判断虚拟地址是否符合 Sv39 canonical 规则。
 * Sv39 只有低 39 位有效，bit 38 是符号位，
 * bit 63:39 必须与 bit 38 相同，否则硬件触发 page fault。
 */
int sv39_virtual_address_is_canonical(uint64_t virtual_address);

/*
 * 取出虚拟地址在指定级别的 9 位 VPN 索引：
 *   level = 2 -> VPN[2]，level = 1 -> VPN[1]，level = 0 -> VPN[0]。
 * 合法返回 0..511；级别越界时返回非法标记。
 */
unsigned int sv39_vpn_index(
    uint64_t virtual_address,
    unsigned int level
);

/*
 * 把物理地址和 flags 编码成 PTE。
 * 物理页号 PPN = physical_address >> 12，
 * 再左移 10 位放进 PTE 的 PPN 字段。
 */
Sv39Pte sv39_make_pte(
    uint64_t physical_address,
    uint64_t flags
);

/* 从 PTE 的 PPN 字段还原物理页基址：PPN << 12。 */
uint64_t sv39_pte_physical_address(Sv39Pte pte);
/* 取出 PTE 的低位权限/状态标志。 */
uint64_t sv39_pte_flags(Sv39Pte pte);
/* 判断 PTE 是否有效：V=1，且不能是 R=0、W=1 的保留组合。 */
int sv39_pte_is_valid(Sv39Pte pte);
/* 判断 PTE 是否是叶 PTE：有效，且 R 或 X 至少一位为 1。 */
int sv39_pte_is_leaf(Sv39Pte pte);

#endif