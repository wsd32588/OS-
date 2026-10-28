#include "mm/vm.h"

#include "mm/page_table_entry.hpp"
#include "mm/pmm.h"

#include <stddef.h>

/*
 * Sv39 页表纯逻辑实现。
 *
 * 只处理 PTE 编码/解码、VPN 索引和有效性判断；
 * 不写 satp、不执行 sfence.vma，
 * 因此可以在宿主机测试中独立验证。
 */
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
    return VirtAddr(address).is_valid();
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
    return VirtPageNum::from_floor(VirtAddr(address)).index(level);
}

/*
 * 由物理地址和 flags 组装 PTE：
 *   1. physical_address >> 12 得到 PPN；
 *   2. PPN << 10 放到 PTE 的 bit 10 起；
 *   3. 低 8 位填入 flags，保持原 C 接口的掩码行为。
 */
Sv39Pte sv39_make_pte(
    uint64_t physical_address,
    uint64_t flags
) {
    return PageTableEntry::from_address(PhysAddr(physical_address), flags).val();
}

/* 从 PTE 的 PPN 字段还原 4 KiB 对齐的物理基址。 */
uint64_t sv39_pte_physical_address(Sv39Pte pte) {
    return PageTableEntry(pte).physical_address().val();
}

/* 取出 PTE 低位的权限/状态标志。 */
uint64_t sv39_pte_flags(Sv39Pte pte) {
    return PageTableEntry(pte).flags();
}

/*
 * 判断 PTE 是否有效。
 *   - V = 1；
 *   - R = 0 且 W = 1 是规范保留的非法组合。
 */
int sv39_pte_is_valid(Sv39Pte pte) {
    return PageTableEntry(pte).is_valid();
}

/*
 * 判断 PTE 是否是叶 PTE（直接映射数据页）。
 * 非叶 PTE 用于指向下一级页表，要求 R = W = X = 0；
 * 叶 PTE 至少 R 或 X 为 1。
 */
int sv39_pte_is_leaf(Sv39Pte pte) {
    return PageTableEntry(pte).is_leaf();
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
    const VirtPageNum vpn = VirtPageNum::from_floor(VirtAddr(virtual_address));

    /*
     * 从 Level 2 (1GB 级) 向下遍历到 Level 1 (2MB 级)
     * 循环退出后停在 Level 0 (4KB 基础页表)
     */
    /* 每轮处理一个非叶级别：level=2 找 L1 表基址，level=1 找 L0 表基址。 */
    for (int level = 2; level > 0; --level) {
        /* 用 VA 的 VPN[level] 作为页表数组下标（0..511）。 */
        unsigned int index = vpn.index(static_cast<unsigned int>(level));
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
            *entry = PageTableEntry::from_address(
                PhysAddr(next_table), SV39_PTE_V).val();
        } else if (!PageTableEntry(*entry).is_valid()) {
            /*页表损坏，单独返回错误*/
            *error_out = SV39_ERR_INVALID_PTE;
            return NULL;
        }
        /*
         * 情况 2：遇到了大页/超大页 (R/W/X 任一位为 1)
         * Sv39 允许在 Level 2 (1GB Gigapage) 或 Level 1 (2MB Megapage) 直接映射物理页。
         * 此函数专用于定位 4KB 叶子节点，如果遇到大页映射则无法继续向下拆分。
         */
        else if (PageTableEntry(*entry).is_leaf()) {
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
        table = PageTableEntry(*entry).physical_address().as_ptr<Sv39PageTable>();
    }

    /* 成功到达 Level 0，通过 VPN[0] 索引起点返回最终的 4KB 页表项指针 */
    *error_out = SV39_OK;
    return &table->entries[vpn.index(0)];
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
int Sv39PageTable::map(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags) {
    /*
     * 参数校验：任何一条不满足都立刻失败，不修改页表。
     *   - flags 只能包含叶子权限位（R/W/X/U/G/A/D），V 由本函数补；
     *   - 叶子必须有 R 或 X；
     *   - R=0、W=1 是规范保留的非法组合。
     */
    const VirtAddr va(virtual_address);
    const PhysAddr pa(physical_address);
    if (!va.is_valid() || !va.is_page_aligned() ||
        !pa.is_valid() || !pa.is_page_aligned() ||
        (flags & ~SV39_PTE_LEAF_FLAG_MASK) != 0 ||        /* 不允许未知位，V 由函数补 */
        (flags & (SV39_PTE_R | SV39_PTE_X)) == 0 ||       /* 叶必须 R 或 X */
        ((flags & SV39_PTE_W) != 0 && (flags & SV39_PTE_R) == 0)
    ) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    /* 走三级页表；create=1 表示缺中间表时自动分配。 */
    int error = SV39_OK;
    Sv39Pte* leaf = sv39_walk_to_leaf(this,
        virtual_address,
        1,
        &error
    );

    if (leaf == NULL) {
        return error;
    }

    /* 该 VA 对应的叶 PTE 已经有效，不允许覆盖。 */
    if ((*leaf & SV39_PTE_V) != 0) {
        if (!PageTableEntry(*leaf).is_valid()) {
            return SV39_ERR_INVALID_PTE;
        }
        return SV39_ERR_ALREADY_MAPPED;
    }

    /* 写入叶 PTE：把 pa 编码成 PPN，flags 补上 V。 */
    *leaf = PageTableEntry::from_address(pa, flags | SV39_PTE_V).val();

    return SV39_OK;
}

int Sv39PageTable::unmap(
    uint64_t virtual_address
) {
    const VirtAddr va(virtual_address);
    if (!va.is_valid() || !va.is_page_aligned()) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    int error = SV39_OK;

    Sv39Pte* leaf = sv39_walk_to_leaf(
        this,
        virtual_address,
        0,
        &error);

    if (leaf == NULL) {
        return error;
    }

    if ((*leaf & SV39_PTE_V) == 0) {
        return SV39_ERR_NOT_MAPPED;
    }

    if (!PageTableEntry(*leaf).is_valid()) {
        return SV39_ERR_INVALID_PTE;
    }

    if (!PageTableEntry(*leaf).is_leaf()) {
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
int Sv39PageTable::query(
    uint64_t virtual_address,
    uint64_t* physical_address_out,
    uint64_t* flags_out
) const {
    /* 输出指针必须可写；查询同样要求 VA canonical。 */
    const VirtAddr va(virtual_address);
    if (physical_address_out == NULL ||
        flags_out == NULL ||
        !va.is_valid()) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    /* create=0：只沿已有页表向下走，不分配新页。 */
    int error = SV39_OK;
    Sv39Pte* leaf = sv39_walk_to_leaf((Sv39PageTable*)this, virtual_address, 0, &error);

    if (leaf == NULL) {
        return error;
    }

    /* 走到一个没有 R/X 的非叶 PTE：这里没有 4 KiB 页映射。 */
    const PageTableEntry entry(*leaf);
    if (!entry.is_leaf()) {
        return SV39_ERR_NOT_MAPPED;
    }

    /* VA 的低 12 位是页内偏移；页表翻译不改变它。 */
    uint64_t page_offset = va.page_offset();

    /* 物理地址 = 页基址 | 页内偏移；页基址低 12 位为 0，等价于相加。 */
    *physical_address_out =
        entry.physical_address().val() | page_offset;

    /* 返回叶 PTE 的 flags（包含 V），调用方可据此判断权限。 */
    *flags_out = entry.flags();
    return SV39_OK;
}

int Sv39PageTable::reclaim_empty_tables(
    uint64_t virtual_address
) {
    const VirtAddr va(virtual_address);
    if (!va.is_valid() || !va.is_page_aligned()) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    /*
     * 记录从根向下的各级页表与对应 entry 槽位：
     * tables[2] = 根页表 (L2)
     * tables[1] = 目录页表 (L1)
     * tables[0] = 叶页表 (L0)
     */
    Sv39PageTable* tables[3];
    Sv39Pte* entries[3];
    int lowest_level = 2;
    tables[2] = this;
    const VirtPageNum vpn = VirtPageNum::from_floor(va);

    /*
     * 第一阶段：自顶向下 (L2 -> L0) 逐级查表并校验
     */
    for (int level = 2; level > 0; --level) {
        const unsigned int index = vpn.index(static_cast<unsigned int>(level));
        Sv39Pte* pte = &tables[level]->entries[index];
        entries[level] = pte;

        /* 若中间路径无效或为大页叶子，说明根本没有下一级独立页表 */
        if ((*pte & SV39_PTE_V) == 0) {
            if (level == 2) {
                return SV39_ERR_NOT_MAPPED;
            }
            break;
        }

        if (!PageTableEntry(*pte).is_valid()) {
            return SV39_ERR_INVALID_PTE;
        }

        if( PageTableEntry(*pte).is_leaf()) {
            return SV39_ERR_INTERMEDIATE_LEAF;
        }

        /*
         * 获取下一级页表的物理基地址并转换为内核可访问指针
         * (当前 TinyOS 依赖内核恒等映射解引用页表物理页)
         */
        tables[level - 1] = PageTableEntry(*pte).physical_address().as_ptr<Sv39PageTable>();
        lowest_level = level - 1;
    }

    /*
     * 第二阶段：自底向上 (L0 -> L1) 检查是否全空，级联回收
     * 注：根页表 (level = 2) 归进程地址空间生命周期管理，不参与此类按需回收
     */
    for (int level = lowest_level; level < 2; ++level) {
        /* 如果当前级别的页表非空，说明其仍承载有效映射，向上级联中断 */
        if (!sv39_page_table_is_empty(tables[level])) {
            break;
        }

        Sv39Pte* parent_entry = entries[level + 1];
        Sv39Pte saved_entry = *parent_entry;

        /* 先将父级指向当前页表的 PTE 抹零，防止悬空访问 */
        *parent_entry = 0;

        /* 释放空页表物理页 */
        if (pmm_free_page(tables[level]) != 0) {
            *parent_entry = saved_entry; /* 释放失败，还原父级 PTE */
            return SV39_ERR_PAGE_FREE_FAILED;
        }
    }

    return SV39_OK;
}

extern "C" {
int sv39_map_page(
    Sv39PageTable* root,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
) {
    if (root == nullptr) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    return root->map(virtual_address, physical_address, flags);
}

int sv39_reclaim_empty_tables(
    Sv39PageTable *root,
    uint64_t virtual_address
) {
    if (root == nullptr) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    return root->reclaim_empty_tables(virtual_address);
}

int sv39_unmap_page(
    Sv39PageTable *root,
    uint64_t virtual_address
) {
    if (root == nullptr) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    return root->unmap(virtual_address);
}

int sv39_query_page(
    const Sv39PageTable* root,
    uint64_t virtual_address,
    uint64_t* physical_address_out,
    uint64_t* flags_out
) {
    if (root == nullptr) {
        return SV39_ERR_INVALID_ARGUMENT;
    }
    return root->query(virtual_address, physical_address_out, flags_out);
}
}
