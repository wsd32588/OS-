#include "vm.h"

/*
 * Sv39 页表纯逻辑实现。
 *
 * 只处理 PTE 编码/解码、VPN 索引和有效性判断；
 * 不写 satp、不执行 sfence.vma，
 * 因此可以在宿主机测试中独立验证。
 */
/* PPN[2:0] 字段掩码：44 位页号左移到 bit 10。 */
#define SV39_PTE_PPN_MASK \
    (((UINT64_C(1) << 44) - 1) << SV39_PTE_PPN_SHIFT)

/*
 * 判断 64 位地址是否符合 Sv39 canonical 规则。
 *
 * Sv39 只使用低 39 位，bit 38 是符号位：
 *   bit 38 = 0 -> bit 63:39 必须全 0；
 *   bit 38 = 1 -> bit 63:39 必须全 1。
 *
 * 这里把高位取成 upper_bits（25 位），
 * sign_bit 是第 38 位，upper_mask 是 25 位全 1。
 */
int sv39_virtual_address_is_canonical(uint64_t address) {
    /* 取出 bit 63:39，共 25 位。 */
    uint64_t upper_bits = address >> 39;
    /* 取出符号位 bit 38。 */
    uint64_t sign_bit = (address >> 38) & UINT64_C(1);
    /* 25 位全 1 的掩码。 */
    uint64_t upper_mask = (UINT64_C(1) << 25) - 1;

    /* bit 38 = 0：高位必须全 0。 */
    if (sign_bit == 0) {
        return upper_bits == 0;
    }

    /* bit 38 = 1：高位必须全 1。 */
    return upper_bits == upper_mask;
}

/*
 * 取出虚拟地址在指定级别的 9 位 VPN 索引。
 *   level = 0 -> VA[20:12]
 *   level = 1 -> VA[29:21]
 *   level = 2 -> VA[38:30]
 * 返回 0..511；level >= 3 返回非法标记。
 */
unsigned int sv39_vpn_index(
    uint64_t address,
    unsigned int level
) {
    /* Sv39 只有 level 0、1、2。 */
    if (level >= SV39_LEVEL_COUNT) {
        return SV39_INVALID_INDEX;
    }

    /* 当前级别字段在 VA 中的起始位：12 + level * 9。 */
    unsigned int shift =
        SV39_PAGE_SHIFT + level * SV39_VPN_BITS;

    /* 取 9 位，范围 0..511。 */
    return (unsigned int)(
        (address >> shift) & (SV39_PTE_COUNT - 1)
    );
}

/*
 * 由物理地址和 flags 组装 PTE：
 *   1. physical_address >> 12 得到 PPN；
 *   2. PPN << 10 放到 PTE 的 bit 10 起；
 *   3. 低 10 位填入 flags。
 */
Sv39Pte sv39_make_pte(
    uint64_t physical_address,
    uint64_t flags
) {
    /* 提取物理页号，并移到 PTE 的 PPN 字段位置。 */
    uint64_t ppn =
        (physical_address >> SV39_PAGE_SHIFT)
        << SV39_PTE_PPN_SHIFT;

    /* 只保留 44 位 PPN 和低 10 位 flags。 */
    return (ppn & SV39_PTE_PPN_MASK) |
        (flags & SV39_PTE_FLAG_MASK);
}

/* 从 PTE 的 PPN 字段还原 4 KiB 对齐的物理基址。 */
uint64_t sv39_pte_physical_address(Sv39Pte pte) {
    /* 取出 PPN 字段。 */
    uint64_t ppn =
        (pte & SV39_PTE_PPN_MASK) >> SV39_PTE_PPN_SHIFT;

    /* 页号左移 12 位得到物理基址。 */
    return ppn << SV39_PAGE_SHIFT;
}

/* 取出 PTE 低位的权限/状态标志。 */
uint64_t sv39_pte_flags(Sv39Pte pte) {
    return pte & SV39_PTE_FLAG_MASK;
}

/*
 * 判断 PTE 是否有效。
 *   - V = 1；
 *   - R = 0 且 W = 1 是规范保留的非法组合。
 */
int sv39_pte_is_valid(Sv39Pte pte) {
    /* V = 0 一律无效。 */
    if ((pte & SV39_PTE_V) == 0) {
        return 0;
    }

    /* R=0、W=1 是规范保留的非法组合。 */
    if ((pte & SV39_PTE_R) == 0 &&
        (pte & SV39_PTE_W) != 0) {
        return 0;
    }

    return 1;
}

/*
 * 判断 PTE 是否是叶 PTE（直接映射数据页）。
 * 非叶 PTE 用于指向下一级页表，要求 R = W = X = 0；
 * 叶 PTE 至少 R 或 X 为 1。
 */
int sv39_pte_is_leaf(Sv39Pte pte) {
    /* 先要求有效，再要求 R 或 X 至少一位为 1。 */
    return sv39_pte_is_valid(pte) &&
        (pte & (SV39_PTE_R | SV39_PTE_X)) != 0;
}