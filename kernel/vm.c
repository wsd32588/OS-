#include "vm.h"

#include "pmm.h"

#include <stddef.h>

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

static int sv39_page_table_is_empty(
    const Sv39PageTable* table
) {
    if (table == NULL) {
        return 0;
    }

    for (size_t i = 0; i < SV39_PTE_COUNT; ++i) {
        if (table->entries[i] != 0) {
            return 0;
        }
    }

    return 1;
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

/*
 * 从 PMM 分配一张根页表。
 *
 * pmm_alloc_page() 返回页对齐、已清零的页首地址；
 * 在开启分页之前，内核是 VA == PA，所以可以把返回的地址
 * 直接当成 Sv39PageTable* 使用。
 *
 * 以后打开 satp 且内核不再恒等映射时，需要把
 * “物理地址 -> 内核直接映射虚拟地址”集中成一个 helper，
 * 不要在每个 cast 处各写一遍。
 */
Sv39PageTable* sv39_create_page_table(void) {
    return (Sv39PageTable*)pmm_alloc_page();
}

/**
 * @brief 遍历 Sv39 三级页表定位指定虚拟地址的叶子页表项（Level 0 PTE）
 *
 * 模拟 MMU 硬件遍历行为（VPN[2] -> VPN[1] -> VPN[0]）。
 * 若途中缺失中间级页表且 create 为真，则动态分配并初始化新页表。
 *
 * @param root            页表根节点（Level 2）
 * @param virtual_address 待查询/映射的虚拟地址
 * @param create          若中间级页表不存在，是否自动分配 (1: 分配, 0: 只读查询)
 * @param error_out       输出错误码 (SV39_OK, SV39_ERR_NOT_MAPPED 等)
 * @return Sv39Pte*       指向 Level 0 的页表项指针；失败返回 NULL
 */
static Sv39Pte* sv39_walk_to_leaf(
    Sv39PageTable* root,
    uint64_t virtual_address,
    int create,
    int* error_out
) {
    /* table 始终指向“当前这一级”的页表；从根（Level 2）开始。 */
    Sv39PageTable* table = root;

    /*
     * 从 Level 2 (1GB 级) 向下遍历到 Level 1 (2MB 级)
     * 循环退出后停在 Level 0 (4KB 基础页表)
     */
    /* 每轮处理一个非叶级别：level=2 找 L1 表基址，level=1 找 L0 表基址。 */
    for (int level = 2; level > 0; --level) {
        /* 用 VA 的 VPN[level] 作为页表数组下标（0..511）。 */
        unsigned int index = sv39_vpn_index(virtual_address, level);
        /* entry 指向当前页表中对应的 PTE。 */
        Sv39Pte* entry = &table->entries[index];

        /* 情况 1：页表项未分配 (V 位为 0) */
        /* V=0：这个 PTE 还没指向任何下一级页表。 */
        if ((*entry & SV39_PTE_V) == 0) {
            // 如果仅是查询映射，直接报错缺页/未映射
            /* 只查询（create=0）时不能建表，直接报告未映射。 */
            if (!create) {
                *error_out = SV39_ERR_NOT_MAPPED;
                return NULL;
            }

            /* 需要建立映射：分配一张新的 4 KiB 页表。 */
            // 分配物理页作为下一级子页表
            /* pmm_alloc_page() 返回前已清零，所以新表的 PTE 全是 0（V=0）。 */
            Sv39PageTable* next_table =
                (Sv39PageTable*)pmm_alloc_page();

            /* 分配失败：物理内存耗尽。 */
            if (next_table == NULL) {
                *error_out = SV39_ERR_NO_MEMORY;
                return NULL;
            }

            // 注意：新分配的物理页必须清零（通常在 pmm 或此处 memset），
            // 否则未初始化的垃圾数据会导致 V 位误判

            // 非叶子节点：只设置有效位 V=1，R/W/X 全置 0
            /*
             * 指针 -> 整数，再交给 sv39_make_pte 编码：
             * 当前内核 VA==PA，且 pmm_alloc_page 返回页对齐地址，
             * 所以这个指针值可以直接当作物理地址。
             * 开启分页后，这里要改成“物理地址 -> 内核直接映射虚拟地址”。
             */
            *entry = sv39_make_pte(
                (uint64_t)(uintptr_t)next_table,
                SV39_PTE_V
            );
        } else if (!sv39_pte_is_valid(*entry)) {
            /*页表损坏，单独返回错误*/
            *error_out = SV39_ERR_INVALID_PTE;
            return NULL;
        }
        /*
         * 情况 2：遇到了大页/超大页 (R/W/X 任一位为 1)
         * Sv39 允许在 Level 2 (1GB Gigapage) 或 Level 1 (2MB Megapage) 直接映射物理页。
         * 此函数专用于定位 4KB 叶子节点，如果遇到大页映射则无法继续向下拆分。
         */
        else if (sv39_pte_is_leaf(*entry)) {
            *error_out = SV39_ERR_INTERMEDIATE_LEAF;
            return NULL;
        }

        /*
         * 从非叶 PTE 的 PPN 还原下一级页表的物理基址，
         * 再把它当作指针继续下一轮。当前 VA==PA，直接 cast 是安全的；
         * 开启分页后这里通常要经过内核 direct map。
         */
        /*
         * 从 PTE 提取下一级页表的物理基地址
         * （注：若开启分页后内核运行在虚拟高地址空间，此处通常需要将物理地址转换为内核虚拟地址）
         */
        table = (Sv39PageTable*)(uintptr_t)
            sv39_pte_physical_address(*entry);
    }

    /* 成功到达 Level 0，通过 VPN[0] 索引起点返回最终的 4KB 页表项指针 */
    *error_out = SV39_OK;
    return &table->entries[sv39_vpn_index(virtual_address, 0)];
}

/*
 * 建立一个 4 KiB 页映射：va -> pa，权限为 flags。
 *
 * 前置条件：
 *   - va 必须 canonical 且 4 KiB 对齐；
 *   - pa 必须 4 KiB 对齐，且能放进 56 位物理地址；
 *   - flags 只允许叶子标志（R/W/X/U/G/A/D），V 由本函数补；
 *   - 叶子必须至少 R 或 X；W 不能单独出现（W=1 必须 R=1）。
 *
 * 中间级页表若不存在，会通过 sv39_walk_to_leaf(create=1) 自动分配。
 */
int sv39_map_page(Sv39PageTable *root,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags) {
    /*
     * 参数校验：任何一条不满足都立刻失败，不修改页表。
     *   - flags 只能包含叶子权限位（R/W/X/U/G/A/D），V 由本函数补；
     *   - 叶子必须有 R 或 X；
     *   - R=0、W=1 是规范保留的非法组合。
     */
    if (root == NULL ||
        !sv39_virtual_address_is_canonical(virtual_address) ||
        (virtual_address & (SV39_PAGE_SIZE - 1U)) != 0 ||
        (physical_address & (SV39_PAGE_SIZE - 1U)) != 0 ||
        (physical_address >> 56U) != 0 ||
        (flags & ~SV39_PTE_LEAF_FLAG_MASK) != 0 ||        /* 不允许未知位，V 由函数补 */
        (flags & (SV39_PTE_R | SV39_PTE_X)) == 0 ||       /* 叶必须 R 或 X */
        ((flags & SV39_PTE_W) != 0 && (flags & SV39_PTE_R) == 0)
    ) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    /* 走三级页表；create=1 表示缺中间表时自动分配。 */
    int error = SV39_OK;
    Sv39Pte* leaf = sv39_walk_to_leaf(root,
        virtual_address,
        1,
        &error
    );

    if (leaf == NULL) {
        return error;
    }

    /* 该 VA 对应的叶 PTE 已经有效，不允许覆盖。 */
    if ((*leaf & SV39_PTE_V) != 0) {
        if (!sv39_pte_is_valid(*leaf)) {
            return SV39_ERR_INVALID_PTE;
        }
        return SV39_ERR_ALREADY_MAPPED;
    }

    /* 写入叶 PTE：把 pa 编码成 PPN，flags 补上 V。 */
    *leaf = sv39_make_pte(physical_address, flags | SV39_PTE_V);

    return SV39_OK;
}

int sv39_unmap_page(
    Sv39PageTable *root,
    uint64_t virtual_address
) {
    if (root == NULL ||
        !sv39_virtual_address_is_canonical(
            virtual_address) ||
        (virtual_address & (SV39_PAGE_SIZE - 1U)) != 0) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    int error = SV39_OK;

    Sv39Pte* leaf = sv39_walk_to_leaf(
        root,
        virtual_address,
        0,
        &error);

    if (leaf == NULL) {
        return error;
    }

    if ((*leaf & SV39_PTE_V) == 0) {
        return SV39_ERR_NOT_MAPPED;
    }

    if (!sv39_pte_is_valid(*leaf)) {
        return SV39_ERR_INVALID_PTE;
    }

    if (!sv39_pte_is_leaf(*leaf)) {
        return SV39_ERR_NOT_MAPPED;
    }

    *leaf = 0;

    return SV39_OK;
}

/*
 * 查询 va 的映射，返回：
 *   - physical_address_out: 翻译后的物理地址（页基址 | VA 的页内偏移）；
 *   - flags_out:            叶子 PTE 的 flags（包含 V）。
 * 未映射或遇到中间大页时返回相应错误码。
 */
int sv39_query_page(
    const Sv39PageTable* root,
    uint64_t virtual_address,
    uint64_t* physical_address_out,
    uint64_t* flags_out
) {
    /* 输出指针必须可写；查询同样要求 VA canonical。 */
    if (root == NULL || physical_address_out == NULL ||
        flags_out == NULL ||
        !sv39_virtual_address_is_canonical(virtual_address)) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    /* create=0：只沿已有页表向下走，不分配新页。 */
    int error = SV39_OK;
    Sv39Pte* leaf = sv39_walk_to_leaf((Sv39PageTable*)root, virtual_address, 0, &error);

    if (leaf == NULL) {
        return error;
    }

    /* 走到一个没有 R/X 的非叶 PTE：这里没有 4 KiB 页映射。 */
    if (!sv39_pte_is_leaf(*leaf)) {
        return SV39_ERR_NOT_MAPPED;
    }

    /* VA 的低 12 位是页内偏移；页表翻译不改变它。 */
    uint64_t page_offset =
        virtual_address & (SV39_PAGE_SIZE - 1U);

    /* 物理地址 = 页基址 | 页内偏移；页基址低 12 位为 0，等价于相加。 */
    *physical_address_out =
        sv39_pte_physical_address(*leaf) | page_offset;

    /* 返回叶 PTE 的 flags（包含 V），调用方可据此判断权限。 */
    *flags_out = sv39_pte_flags(*leaf);
    return SV39_OK;
}

int sv39_reclaim_empty_tables(
    Sv39PageTable *root,
    uint64_t virtual_address
) {
    if (root == NULL ||
        !sv39_virtual_address_is_canonical(
            virtual_address) ||
        (virtual_address & (SV39_PAGE_SIZE - 1U)) != 0
    ) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    int error = SV39_OK;

    Sv39Pte* leaf = sv39_walk_to_leaf(
        root,
        virtual_address,
        0,
        &error);
    if (leaf == NULL) {
        return error;
    }

    Sv39Pte* level2_entry =
        &root->entries[
            sv39_vpn_index(
                virtual_address,
                2)
        ];

    if (!sv39_pte_is_valid(*level2_entry) || sv39_pte_is_leaf(*level2_entry)) {
        return SV39_ERR_NOT_MAPPED;
    }

    Sv39PageTable* level1_table =
        (Sv39PageTable*)(uintptr_t)
        sv39_pte_physical_address(*level2_entry);
    Sv39Pte* level1_entry =
        &level1_table->entries[
            sv39_vpn_index(
                virtual_address,
                1)
        ];

    Sv39PageTable* level0_table =
        (Sv39PageTable*)(uintptr_t)
            sv39_pte_physical_address(*level1_entry);

    if (!sv39_page_table_is_empty(level0_table)) {
        return 0;
    }

    Sv39Pte saved_level1_entry = *level1_entry;
    *level1_entry = 0;

    if (pmm_free_page(level0_table) != 0) {
        *level1_entry = saved_level1_entry;
        return SV39_ERR_PAGE_FREE_FAILED;
    }
    if (!sv39_page_table_is_empty(level1_table)) {
        return SV39_OK;
    }

    Sv39Pte saved_level2_entry = *level2_entry;
    *level2_entry = 0;

    if (pmm_free_page(level1_table) != 0) {
        *level2_entry = saved_level2_entry;
        return SV39_ERR_PAGE_FREE_FAILED;
    }

    return SV39_OK;
}
