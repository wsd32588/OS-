#include "test_vm.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "pmm.h"
#include "vm.h"

static int fail(
    const char* msg,
    const char* file,
    unsigned long line
) {
    fprintf(stderr,"[FAIL] %s at %s:%lu\n", msg, file, line);
    return -1;
}

static void print_u64_value(FILE* stream, uint64_t value) {
    fprintf(stream, "%"PRIu64, value);

    /*
     * 错误码是负的 int，但 CHECK_EQ 统一按 uint64_t 比较；
     * 这个函数额外打印它的有符号解释，避免看到 18446744073709551611。
     */
    if (value > (uint64_t)INT64_MAX) {
        fprintf(stream, " (signed: %"PRId64")", (int64_t)value);
    }
}

static int fail_eq_u64(
    uint64_t actual,
    uint64_t expected,
    const char* actual_expression,
    const char* expected_expression,
    const char* file,
    unsigned long line
) {
    fprintf(
        stderr,
        "[FAIL] %s == %s at %s:%lu\n"
        "    actual:       ",
        actual_expression,
        expected_expression,
        file,
        line
    );
    print_u64_value(stderr, actual);
    fprintf(stderr, "\n    expected:     ");
    print_u64_value(stderr, expected);
    fprintf(stderr, "\n");

    return -1;
}

#define CHECK_EQ(actual_expression, expected_expression) \
    do{ \
        uint64_t actual_value = \
            (uint64_t)(actual_expression); \
        uint64_t expected_value = \
            (uint64_t)(expected_expression); \
        if (actual_value != expected_value) { \
            return fail_eq_u64( \
                actual_value, \
                expected_value, \
                #actual_expression, \
                #expected_expression, \
                __FILE__, \
                __LINE__ \
            ); \
        } \
    } while(0)

#define CHECK(cond) \
    do {if (!(cond)) { return fail(#cond, __FILE__, __LINE__); } }while(0)

static int test_size(void) {
    CHECK_EQ(sizeof(Sv39Pte), 8);
    CHECK_EQ(SV39_PTE_COUNT, 512);
    CHECK_EQ(sizeof(Sv39PageTable), SV39_PAGE_SIZE);
    return 0;
}

static int test_canonical(void) {
    CHECK(sv39_virtual_address_is_canonical(UINT64_C(0)));
    CHECK(sv39_virtual_address_is_canonical(UINT64_C(0x0000003fffffffff)));
    CHECK(sv39_virtual_address_is_canonical(UINT64_C(0xffffffc000000000)));
    CHECK(sv39_virtual_address_is_canonical(UINT64_C(0xffffffffffffffff)));

    CHECK(!sv39_virtual_address_is_canonical(UINT64_C(0x0000004000000000)));
    CHECK(!sv39_virtual_address_is_canonical(UINT64_C(0x0000007fffffffff)));
    CHECK(!sv39_virtual_address_is_canonical(UINT64_C(0xffffffbfffffffff)));
    return 0;
}

static int test_vpn_index(void) {
    const uint64_t v2 = 0x0ab;
    const uint64_t v1 = 0x123;
    const uint64_t v0 = 0x1ff;
    const uint64_t off = 0x456;
    const uint64_t va =
        (v2 << 30) |
        (v1 << 21) |
        (v0 << 12) |
        off;

    CHECK_EQ(sv39_vpn_index(va,2),v2);
    CHECK_EQ(sv39_vpn_index(va,1),v1);
    CHECK_EQ(sv39_vpn_index(va,0),v0);
    CHECK_EQ(sv39_vpn_index(va,3),SV39_INVALID_INDEX);

    return 0;
}

static int test_pte_roundtrip(void) {
    static const uint64_t pas[] = {
        UINT64_C(0x0),
        UINT64_C(0x1000),
        UINT64_C(0x80208000),
        ((UINT64_C(1) << 44) - 1) << 12,
    };

    const uint64_t flags =
        SV39_PTE_V | SV39_PTE_R | SV39_PTE_W |
        SV39_PTE_X | SV39_PTE_U | SV39_PTE_G |
        SV39_PTE_A | SV39_PTE_D;

    for (size_t i = 0; i < sizeof(pas) / sizeof(pas[0]); ++i) {
        Sv39Pte pte = sv39_make_pte(pas[i],flags);
        CHECK_EQ(sv39_pte_physical_address(pte),pas[i]);
        CHECK_EQ(sv39_pte_flags(pte), flags & SV39_PTE_FLAG_MASK);
    }

    CHECK_EQ(sv39_make_pte(UINT64_C(0x1000),0),
            UINT64_C(1) << SV39_PTE_PPN_SHIFT);
    return 0;
}

static int test_validity_and_leaf(void) {
    CHECK(!sv39_pte_is_valid(UINT64_C(0)));
    CHECK(!sv39_pte_is_valid(SV39_PTE_V | SV39_PTE_W));
    CHECK(sv39_pte_is_valid(SV39_PTE_V));
    CHECK(sv39_pte_is_valid(SV39_PTE_V | SV39_PTE_R));
    CHECK(sv39_pte_is_valid(SV39_PTE_V | SV39_PTE_X));

    CHECK(!sv39_pte_is_leaf(SV39_PTE_V));
    CHECK(sv39_pte_is_leaf(SV39_PTE_V | SV39_PTE_R));
    CHECK(sv39_pte_is_leaf(SV39_PTE_V | SV39_PTE_X));
    CHECK(!sv39_pte_is_leaf(UINT64_C(0)));
    return 0;
}

static uint32_t next_random(uint32_t *state) {
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static int test_stress(void) {
    uint32_t state = UINT32_C(0x39);

    for (unsigned long i = 0; i < 10000; ++i) {
        uint64_t random_bits =
            ((uint64_t)next_random(&state) << 32) |
            (uint64_t)next_random(&state);
        uint64_t ppn = random_bits &
            ((UINT64_C(1) << 44) - 1);
        uint64_t pa = ppn << 12;
        uint64_t flags = next_random(&state) & SV39_PTE_FLAG_MASK;

        Sv39Pte pte = sv39_make_pte(pa, flags);
        CHECK_EQ(sv39_pte_physical_address(pte), pa);
        CHECK_EQ(sv39_pte_flags(pte), flags);
    }

    return 0;
}

static int test_sv39_map_and_query(void) {
    /*
     * 1. 给 PMM 一块页对齐的“物理内存”。
     *    Makefile 已经把 kernel/vm.c 和 kernel/pmm.c 一起编进宿主测试，
     *    所以这里可以用真实的 pmm_alloc_page()。
     */
    enum { REGION_PAGES = 64 };
    static uint8_t memory_pool[REGION_PAGES * SV39_PAGE_SIZE]
        __attribute__((aligned(SV39_PAGE_SIZE)));
    void *region = memory_pool;

    if (pmm_init(region, memory_pool + sizeof(memory_pool)) != 0) {
        return fail("pmm_init for vm test", __FILE__, __LINE__);
    }

    /* 2. 创建根页表：pmm_alloc_page() 返回前会清零。 */
    Sv39PageTable *root = sv39_create_page_table();
    CHECK(root != NULL);

    /*
     * 3. 选一个 canonical、4K 对齐，且 VPN[2]/VPN[1]/VPN[0] 都非零的 VA。
     *    v2 的 bit 8 对应 VA 的 bit 38；v2 < 0x100 时是 canonical positive。
     */
    const uint64_t v2 = 0x001;
    const uint64_t v1 = 0x002;
    const uint64_t v0 = 0x003;
    const uint64_t va = (v2 << 30) | (v1 << 21) | (v0 << 12);

    const uint64_t pa = UINT64_C(0x80208000);   /* 4K 对齐，< 2^56 */
    const uint64_t flags = SV39_PTE_R | SV39_PTE_W | SV39_PTE_A | SV39_PTE_D;
    const uint64_t noncanonical_alias = va | (UINT64_C(1) << 63);

    /* 4. 建立映射并查询 */
    CHECK(sv39_map_page(root, va, pa, flags) == SV39_OK);

    uint64_t out_pa = 0;
    uint64_t out_flags = 0;
    CHECK(sv39_query_page(root, va, &out_pa, &out_flags) == SV39_OK);
    CHECK_EQ(out_pa, pa);
    CHECK_EQ(out_flags, flags | SV39_PTE_V);   /* map 会自动补 V */

    /* 5. 同一个页加页内偏移：query 应返回 物理基址 | offset */
    const uint64_t offset = UINT64_C(0x123);
    CHECK(sv39_query_page(root, va + offset, &out_pa, &out_flags) == SV39_OK);
    CHECK_EQ(out_pa, pa | offset);
    CHECK_EQ(out_flags, flags | SV39_PTE_V);

    /* 6. 未映射的邻居应返回 NOT_MAPPED */
    CHECK(sv39_query_page(root,
        va + SV39_PAGE_SIZE,
        &out_pa, &out_flags) == SV39_ERR_NOT_MAPPED);
    CHECK(sv39_query_page(root,
        noncanonical_alias, &out_pa,
        &out_flags) == SV39_ERR_INVALID_ARGUMENT);

    /* 7. 白盒检查：确认真的建出了 VPN2 -> VPN1 -> VPN0 三级链 */
    Sv39Pte *e2 = &root->entries[sv39_vpn_index(va, 2)];
    CHECK(sv39_pte_is_valid(*e2));
    CHECK(!sv39_pte_is_leaf(*e2));

    Sv39PageTable *l1 =
        (Sv39PageTable *)(uintptr_t)sv39_pte_physical_address(*e2);
    CHECK(l1 != NULL);

    Sv39Pte *e1 = &l1->entries[sv39_vpn_index(va, 1)];
    CHECK(sv39_pte_is_valid(*e1));
    CHECK(!sv39_pte_is_leaf(*e1));

    Sv39PageTable *l0 =
        (Sv39PageTable *)(uintptr_t)sv39_pte_physical_address(*e1);
    CHECK(l0 != NULL);

    Sv39Pte *e0 = &l0->entries[sv39_vpn_index(va, 0)];
    CHECK(sv39_pte_is_leaf(*e0));
    CHECK_EQ(sv39_pte_physical_address(*e0), pa);
    CHECK_EQ(sv39_pte_flags(*e0), flags | SV39_PTE_V);

    /* 8. 同一 VA 重复映射 */
    CHECK(sv39_map_page(root, va, pa, flags) == SV39_ERR_ALREADY_MAPPED);

    /* 9. 非法参数 */
    CHECK(sv39_map_page(NULL, va, pa, flags) == SV39_ERR_INVALID_ARGUMENT);

    /* 非 canonical：bit 38 置 1 但高位全 0 */
    CHECK(sv39_map_page(root, UINT64_C(0x0000004000000000), pa, flags)
          == SV39_ERR_INVALID_ARGUMENT);

    /* PA 未 4K 对齐 */
    CHECK(sv39_map_page(root, va, pa + 1, flags)
          == SV39_ERR_INVALID_ARGUMENT);

    /* W=1 但 R=0：保留非法组合 */
    CHECK(sv39_map_page(root, va, pa, SV39_PTE_W)
          == SV39_ERR_INVALID_ARGUMENT);

    /* query 的空指针 */
    CHECK(sv39_query_page(NULL, va, &out_pa, &out_flags)
          == SV39_ERR_INVALID_ARGUMENT);
    CHECK(sv39_query_page(root, va, NULL, &out_flags)
          == SV39_ERR_INVALID_ARGUMENT);
    CHECK(sv39_query_page(root, va, &out_pa, NULL)
          == SV39_ERR_INVALID_ARGUMENT);

    return 0;
}

static int test_sv39_mapping_stress(void) {
    enum {
        REGION_PAGES = 64,
        MAPPING_COUNT = 4096
    };

    static uint8_t memory_pool[REGION_PAGES * SV39_PAGE_SIZE]
        __attribute__((aligned(SV39_PAGE_SIZE)));
    void* region = memory_pool;

    if (pmm_init(region, memory_pool + sizeof(memory_pool)) != 0) {
        return fail("pmm_init for SV39 stress test",
            __FILE__, __LINE__);
    }
    CHECK_EQ(
        pmm_available_pages(),
        REGION_PAGES
    );

    Sv39PageTable* root = sv39_create_page_table();
    if (root == NULL) {
        return fail("could not allocate SV39 root table",
            __FILE__, __LINE__);
    }
    CHECK_EQ(
        pmm_available_pages(),
        REGION_PAGES - 1
    );

    /*
     * 8 GiB，是合法的低 canonical 地址，
     * 同时按 1 GiB、2 MiB 和 4 KiB 对齐。
     */
    const uint64_t virtual_base = UINT64_C(0x0000000200000000);
    const uint64_t physical_base = UINT64_C(0x0000000100000000);
    const uint64_t flags =
        SV39_PTE_R | SV39_PTE_W |
        SV39_PTE_A | SV39_PTE_D;

    /*
     *先全部映射再统一查找
     *每映射一页随后查询一页很难发现后面映射破坏前面映射的问题
     */
    for (size_t i = 0; i < MAPPING_COUNT; ++i) {
        uint64_t virtual_address = virtual_base + (uint64_t)i * SV39_PAGE_SIZE;
        uint64_t physical_address = physical_base + (uint64_t)i * SV39_PAGE_SIZE;
        CHECK(
            sv39_map_page(
                root,
                virtual_address,
                physical_address,
                flags
            ) == SV39_OK
        );

        if (i == 0) {
        CHECK_EQ(
            pmm_available_pages(),
            REGION_PAGES - 3
        );
}
    }

    /*
     *4096页 = 16MiB
     *需要1张根页表， 1张Level 1页表，
     *8张Level 0页表
     *共计消耗10个PMM页面。剩余54页
     */
    CHECK_EQ(
        pmm_available_pages(),
        REGION_PAGES - 10
    );

    for (size_t i = 0; i < MAPPING_COUNT; ++i) {
        uint64_t page_offset =
            ((uint64_t)i * UINT64_C(37)) &
            (SV39_PAGE_SIZE - 1U);
        uint64_t virtual_address =
            virtual_base +
            (uint64_t)i * SV39_PAGE_SIZE +
            page_offset;
        uint64_t expected_physical_address =
            physical_base +
            (uint64_t)i * SV39_PAGE_SIZE +
            page_offset;

        uint64_t actual_physical_address = 0;
        uint64_t actual_flags = 0;

        CHECK(
            sv39_query_page(root,
                virtual_address,
                &actual_physical_address,
                &actual_flags
            ) == SV39_OK
        );

        CHECK_EQ(
            actual_physical_address,
            expected_physical_address
        );

        CHECK_EQ(
            actual_flags,
            flags | SV39_PTE_V
        );
    }

    const uint64_t malformed_va =
    UINT64_C(0x0000001000000000);

    unsigned int malformed_index =
        sv39_vpn_index(malformed_va, 2);

    Sv39Pte malformed_pte =
        sv39_make_pte(
            UINT64_C(0),
            SV39_PTE_V | SV39_PTE_W
        );

    root->entries[malformed_index] = malformed_pte;

    unsigned long pages_before =
        pmm_available_pages();

    CHECK(
        sv39_map_page(
            root,
            malformed_va,
            UINT64_C(0x80400000),
            SV39_PTE_R
        ) == SV39_ERR_INVALID_PTE
    );

    /* 失败不能偷偷分配新页表。 */
    CHECK_EQ(
        pmm_available_pages(),
        pages_before
    );

    /* 失败也不能覆盖原来的非法项。 */
    CHECK_EQ(
        root->entries[malformed_index],
        malformed_pte
    );

    return 0;
}

static int test_sv39_corrupted_pte_rejection(void) {
    /* 初始化测试物理内存并分配根页表 */
    static uint8_t memory_pool[8 * SV39_PAGE_SIZE] __attribute__((aligned(4096)));
    CHECK_EQ(pmm_init(memory_pool, memory_pool + sizeof(memory_pool)), 0);

    Sv39PageTable* root = sv39_create_page_table();
    CHECK_EQ(root != NULL,1);

    /* 选取同一个 Level 0 页表内的两个相邻 4KB 虚拟地址 */
    uint64_t va1 = 0x1000;
    uint64_t va2 = 0x2000;
    uint64_t pa1 = 0x80000000;
    uint64_t pa2 = 0x80001000;

    /* 建立合法基准映射 */
    CHECK_EQ(sv39_map_page(root, va1, pa1, SV39_PTE_R | SV39_PTE_W), SV39_OK);

    /* 利用公开结构体定位到相邻的 Level 0 PTE，人工篡改成非法的 V | W */
    unsigned int vpn2 = sv39_vpn_index(va2, 2);
    unsigned int vpn1 = sv39_vpn_index(va2, 1);
    unsigned int vpn0 = sv39_vpn_index(va2, 0);

    Sv39PageTable* l1_table = (Sv39PageTable*)(uintptr_t)
        sv39_pte_physical_address(root->entries[vpn2]);
    Sv39PageTable* l0_table = (Sv39PageTable*)(uintptr_t)
        sv39_pte_physical_address(l1_table->entries[vpn1]);

    l0_table->entries[vpn0] = SV39_PTE_V | SV39_PTE_W;

    CHECK_EQ(
        sv39_unmap_page(root, va2),
        SV39_ERR_INVALID_PTE
    );

    CHECK_EQ(
        l0_table->entries[vpn0],
        SV39_PTE_V | SV39_PTE_W
    );
    /* 记录映射前的PMM可用页数 */
    unsigned long pmm_pages_before = pmm_available_pages();

    /* 映射VA2 */
    int map_let = sv39_map_page(root, va2, pa2, SV39_PTE_R | SV39_PTE_W);
    CHECK_EQ(map_let, SV39_ERR_INVALID_PTE);

    /* 检查前后PMM是否发生改变 */
    CHECK_EQ(pmm_available_pages(),pmm_pages_before);

    /* 验证非法 PTE 没有被 map 覆盖篡改 */
    CHECK_EQ(l0_table->entries[vpn0],SV39_PTE_V | SV39_PTE_W);

    /* 确认确实没有形成合法映射 */
    uint64_t query_pa = 0, query_flags = 0;
    CHECK_EQ(sv39_query_page(root, va2, &query_pa, &query_flags), SV39_ERR_NOT_MAPPED);

    l0_table->entries[vpn0] = SV39_PTE_V;

    CHECK_EQ(
        sv39_unmap_page(root, va2),
        SV39_ERR_NOT_MAPPED
    );

    CHECK_EQ(
        l0_table->entries[vpn0],
        SV39_PTE_V
    );

    return 0; // 测试通过
}

static int test_sv39_unmap_page(void) {
    /* 初始化物理内存并分配根页表 */
    static uint8_t memory_pool[
        8 * SV39_PAGE_SIZE
    ] __attribute__((aligned(SV39_PAGE_SIZE)));

    CHECK_EQ(
        pmm_init(
            memory_pool,
            memory_pool + sizeof(memory_pool)
        ),
        0
    );
    Sv39PageTable* root = sv39_create_page_table();
    CHECK(root != NULL);

    /* 选定同一个根页表内相邻4KiB的虚拟地址 */
    const uint64_t va1 = UINT64_C(0x400000);
    const uint64_t va2 = va1 + SV39_PAGE_SIZE;

    const uint64_t pa1 = UINT64_C(0x81000000);
    const uint64_t pa2 = UINT64_C(0x82000000);
    CHECK_EQ(
        sv39_unmap_page(NULL, va1),
        SV39_ERR_INVALID_ARGUMENT
    );

    CHECK_EQ(
        sv39_unmap_page(
            root,
            UINT64_C(0x0000004000000000)
        ),
        SV39_ERR_INVALID_ARGUMENT
    );

    /* 检查unmap映射时候是否隐式建立新的L1/L0页表 */
    const uint64_t never_mapped_va =
        UINT64_C(0x400000000);
    unsigned long pages_before_missing_unmap =
        pmm_available_pages();

    CHECK_EQ(
        sv39_unmap_page(root, never_mapped_va),
        SV39_ERR_NOT_MAPPED
    );

    CHECK_EQ(
        pmm_available_pages(),
        pages_before_missing_unmap
    );

    const uint64_t large_page_va =
        UINT64_C(0x80000000);
    unsigned long large_page_index =
        sv39_vpn_index(large_page_va, 2);

    Sv39Pte large_page_pte =
        sv39_make_pte(
            UINT64_C(0x80000000),
            SV39_PTE_V | SV39_PTE_R | SV39_PTE_A);
    root->entries[large_page_index] = large_page_pte;

    CHECK_EQ(
        sv39_unmap_page(root, large_page_va),
        SV39_ERR_INTERMEDIATE_LEAF
    );

    CHECK_EQ(
        root->entries[large_page_index],
        large_page_pte
    );

    /* 未对齐的 VA 必须被 unmap 拒绝 */
    CHECK_EQ(sv39_unmap_page(root, va1 + 1), SV39_ERR_INVALID_ARGUMENT);

    /* 建立合法基准映射并检查是否正确映射到相关地址 */
    int map1 = sv39_map_page(root,
        va1,
        pa1,
        SV39_PTE_R);
    CHECK_EQ(map1, SV39_OK);

    int map2 = sv39_map_page(root,
        va2,
        pa2,
        SV39_PTE_R);
    CHECK_EQ(map2, SV39_OK);

    uint64_t physical_address_out = 0;
    uint64_t flag_out = 0;

    /* 查询映射是否正确，是否造成映射物理地址与实际结果不同以及权限检查 */
    CHECK_EQ(
        sv39_query_page(root, va1, &physical_address_out, &flag_out), SV39_OK);
    CHECK_EQ(pa1, physical_address_out);
    CHECK_EQ(SV39_PTE_V | SV39_PTE_R, flag_out);

    CHECK_EQ(
        sv39_query_page(root, va2, &physical_address_out, &flag_out), SV39_OK);
    CHECK_EQ(pa2, physical_address_out);
    CHECK_EQ(SV39_PTE_V | SV39_PTE_R, flag_out);

    /* 存下分配后的可用页表数量 */
    unsigned long pages_before_unmap = pmm_available_pages();

    /* 解除va1相关的映射表并检查 */
    CHECK_EQ(sv39_unmap_page(root, va1), SV39_OK);
    CHECK_EQ(
        sv39_query_page(root, va1, &physical_address_out, &flag_out), SV39_ERR_NOT_MAPPED);

    /* 检查是否影响到va2的映射 */
    CHECK_EQ(sv39_query_page(root, va2, &physical_address_out, &flag_out), SV39_OK);
    CHECK_EQ(pa2, physical_address_out);
    CHECK_EQ(SV39_PTE_V | SV39_PTE_R, flag_out);

    /*  PMM可用页表必须等于pages_before-unmap */
    CHECK_EQ(pmm_available_pages(), pages_before_unmap);
    CHECK_EQ(sv39_unmap_page(root, va1), SV39_ERR_NOT_MAPPED);

    /* 彻底解除邻居 va2 的映射，此时 root 下的所有叶子映射全部归零 */
    CHECK_EQ(sv39_unmap_page(root, va2), SV39_OK);
    CHECK_EQ(sv39_query_page(root, va2, &physical_address_out, &flag_out), SV39_ERR_NOT_MAPPED);

    /* 在清空的地址映射上，强行把 va1 重新映射到邻居的 pa2  */
    int remap_cross = sv39_map_page(root, va1, pa2, SV39_PTE_R);
    CHECK_EQ(remap_cross, SV39_OK);

    /* 再次使用 query 模块，验证 va1 的最新物理主人是不是变成了 pa2 */
    CHECK_EQ(sv39_query_page(root, va1, &physical_address_out, &flag_out), SV39_OK);
    CHECK_EQ(pa2, physical_address_out); /* 物理地址必须绝对等于 pa2，不能有任何旧地址污染 */
    CHECK_EQ(SV39_PTE_V | SV39_PTE_R, flag_out);

    CHECK_EQ(
        sv39_map_page(root, va1, pa1, SV39_PTE_R),
        SV39_ERR_ALREADY_MAPPED
    );
    CHECK_EQ(sv39_unmap_page(root, va1), SV39_OK);
    CHECK_EQ(
        sv39_unmap_page(root, va1),
        SV39_ERR_NOT_MAPPED
    );

    /* 彻底擦除善后，保证测试结束后的绝对纯净 */
    CHECK_EQ(sv39_query_page(root, va1, &physical_address_out, &flag_out), SV39_ERR_NOT_MAPPED);

    return 0;
}

static int test_sv39_reclaim_empty_tables(void) {
    /*
     * 8 页的小 PMM：root + L1 + L0_A + L0_B 之后剩 4 页。
     * 用静态、页对齐的池，失败路径也不会泄漏。
     */
    static uint8_t memory_pool[8 * SV39_PAGE_SIZE]
        __attribute__((aligned(SV39_PAGE_SIZE)));

    CHECK_EQ(pmm_init(memory_pool, memory_pool + sizeof(memory_pool)), 0);
    const unsigned long pages_initial = pmm_available_pages();

    Sv39PageTable* root = sv39_create_page_table();
    CHECK(root != NULL);

    /*
     * va1 和 va2 落在同一张 L0_A（同 VPN[2]/VPN[1]，不同 VPN[0]）；
     * va3 落在同一张 L1 下的另一张 L0_B（同 VPN[2]，不同 VPN[1]）。
     */
    const uint64_t va1 = UINT64_C(0x00400000);
    const uint64_t va2 = va1 + SV39_PAGE_SIZE;
    const uint64_t va3 = va1 + (UINT64_C(1) << 21);

    const uint64_t pa1 = UINT64_C(0x81000000);
    const uint64_t pa2 = UINT64_C(0x82000000);
    const uint64_t pa3 = UINT64_C(0x83000000);

    CHECK_EQ(sv39_map_page(root, va1, pa1, SV39_PTE_R), SV39_OK);
    CHECK_EQ(sv39_map_page(root, va2, pa2, SV39_PTE_R), SV39_OK);
    CHECK_EQ(sv39_map_page(root, va3, pa3, SV39_PTE_R), SV39_OK);

    /* 记录映射完成后的可用页数 */
    const unsigned long pages_after_map = pmm_available_pages();

    /* 记录 L1 / L0_A / L0_B 的地址，用于验证“没提前释放”和“被复用” */
    const unsigned int vpn2 = sv39_vpn_index(va1, 2);
    const unsigned int l1_index_va1 = sv39_vpn_index(va1, 1);
    const unsigned int l1_index_va3 = sv39_vpn_index(va3, 1);

    Sv39Pte* root_entry = &root->entries[vpn2];
    CHECK(sv39_pte_is_valid(*root_entry));
    CHECK(!sv39_pte_is_leaf(*root_entry));

    Sv39PageTable* l1 =
        (Sv39PageTable*)(uintptr_t)sv39_pte_physical_address(*root_entry);
    const uintptr_t l1_address = (uintptr_t)l1;

    Sv39Pte* l1_entry_l0_a = &l1->entries[l1_index_va1];
    CHECK(sv39_pte_is_valid(*l1_entry_l0_a));
    CHECK(!sv39_pte_is_leaf(*l1_entry_l0_a));
    Sv39PageTable* l0_a =
        (Sv39PageTable*)(uintptr_t)sv39_pte_physical_address(*l1_entry_l0_a);
    const uintptr_t l0_a_address = (uintptr_t)l0_a;
    const Sv39Pte l0_a_entry_before = *l1_entry_l0_a;

    Sv39Pte* l1_entry_l0_b = &l1->entries[l1_index_va3];
    CHECK(sv39_pte_is_valid(*l1_entry_l0_b));
    CHECK(!sv39_pte_is_leaf(*l1_entry_l0_b));
    Sv39PageTable* l0_b =
        (Sv39PageTable*)(uintptr_t)sv39_pte_physical_address(*l1_entry_l0_b);
    const uintptr_t l0_b_address = (uintptr_t)l0_b;

    uint64_t pa_out = 0;
    uint64_t flags_out = 0;

    /* ---------- 1) unmap va1：va2 还在同一张 L0_A，不应回收 ---------- */
    CHECK_EQ(sv39_unmap_page(root, va1), SV39_OK);
    CHECK_EQ(sv39_reclaim_empty_tables(root, va1), SV39_OK);

    CHECK_EQ(pmm_available_pages(), pages_after_map);
    CHECK_EQ(*l1_entry_l0_a, l0_a_entry_before);   /* L0_A 没被释放/改动 */
    CHECK_EQ(sv39_query_page(root, va1, &pa_out, &flags_out),
             SV39_ERR_NOT_MAPPED);
    CHECK_EQ(sv39_query_page(root, va2, &pa_out, &flags_out), SV39_OK);
    CHECK_EQ(pa_out, pa2);
    CHECK_EQ(sv39_query_page(root, va3, &pa_out, &flags_out), SV39_OK);
    CHECK_EQ(pa_out, pa3);

    /* ---------- 2) unmap va2：L0_A 变空被释放，L1 仍指向 L0_B ---------- */
    CHECK_EQ(sv39_unmap_page(root, va2), SV39_OK);
    CHECK_EQ(sv39_reclaim_empty_tables(root, va2), SV39_OK);

    CHECK_EQ(pmm_available_pages(), pages_after_map + 1);
    CHECK_EQ(*l1_entry_l0_a, 0);                   /* 指向 L0_A 的项清 0 */
    CHECK_EQ((uintptr_t)sv39_pte_physical_address(*root_entry), l1_address);
    CHECK_EQ(sv39_query_page(root, va2, &pa_out, &flags_out),
             SV39_ERR_NOT_MAPPED);
    CHECK_EQ(sv39_query_page(root, va3, &pa_out, &flags_out), SV39_OK);
    CHECK_EQ(pa_out, pa3);

    /* ---------- 3) unmap va3：L0_B 和 L1 都变空，均被释放 ---------- */
    CHECK_EQ(sv39_unmap_page(root, va3), SV39_OK);
    CHECK_EQ(sv39_reclaim_empty_tables(root, va3), SV39_OK);

    CHECK_EQ(pmm_available_pages(), pages_after_map + 3);
    CHECK_EQ(pmm_available_pages(), pages_initial - 1);   /* 只剩 root */
    CHECK_EQ(*root_entry, 0);
    CHECK_EQ(sv39_query_page(root, va3, &pa_out, &flags_out),
             SV39_ERR_NOT_MAPPED);

    CHECK_EQ(sv39_reclaim_empty_tables(NULL, va1),
            SV39_ERR_INVALID_ARGUMENT);

    CHECK_EQ(sv39_reclaim_empty_tables(root, va1 + 1),
            SV39_ERR_INVALID_ARGUMENT);

    CHECK_EQ(sv39_reclaim_empty_tables(
            root,
            UINT64_C(0x0000004000000000)
        ),
        SV39_ERR_INVALID_ARGUMENT);

    /* ---------- 4) 重新 map va1：应该复用 free list 里的 L1 和 L0 ---------- */
    CHECK_EQ(sv39_map_page(root, va1, pa1, SV39_PTE_R), SV39_OK);
    CHECK_EQ(pmm_available_pages(), pages_after_map + 1);  /* 重新消耗 2 页 */

    CHECK_EQ(sv39_query_page(root, va1, &pa_out, &flags_out), SV39_OK);
    CHECK_EQ(pa_out, pa1);
    CHECK_EQ(flags_out, SV39_PTE_V | SV39_PTE_R);

    /* 新的 L1 / L0 必须来自刚才释放掉的三页之一，证明 free list 真的被复用 */
    CHECK(sv39_pte_is_valid(*root_entry));
    CHECK(!sv39_pte_is_leaf(*root_entry));
    const uintptr_t new_l1_address =
        (uintptr_t)sv39_pte_physical_address(*root_entry);
    CHECK(new_l1_address == l1_address ||
          new_l1_address == l0_a_address ||
          new_l1_address == l0_b_address);

    Sv39PageTable* new_l1 =
        (Sv39PageTable*)(uintptr_t)new_l1_address;
    Sv39Pte* new_l0_entry = &new_l1->entries[l1_index_va1];
    CHECK(sv39_pte_is_valid(*new_l0_entry));
    CHECK(!sv39_pte_is_leaf(*new_l0_entry));
    const uintptr_t new_l0_address =
        (uintptr_t)sv39_pte_physical_address(*new_l0_entry);
    CHECK(new_l0_address == l1_address ||
          new_l0_address == l0_a_address ||
          new_l0_address == l0_b_address);
    CHECK(new_l0_address != new_l1_address);

    const uint64_t missing_va = UINT64_C(0x400000000);
    unsigned long pages_before_missing =
        pmm_available_pages();

    CHECK_EQ(
        sv39_reclaim_empty_tables(root, missing_va),
        SV39_ERR_NOT_MAPPED
    );

    CHECK_EQ(
        pmm_available_pages(),
        pages_before_missing
    );

    return 0;
}

int test_vm(void) {
    if (test_size() != 0) return -1;
    if (test_canonical() != 0) return -1;
    if (test_vpn_index() != 0) return -1;
    if (test_pte_roundtrip() != 0) return -1;
    if (test_validity_and_leaf() != 0) return -1;
    if (test_stress() != 0) return -1;
    if (test_sv39_map_and_query() != 0) return -1;
    if (test_sv39_mapping_stress() != 0) {
        return -1;
    }
    if(test_sv39_corrupted_pte_rejection() != 0) {
        return -1;
    }
    if (test_sv39_unmap_page() != 0) {
        return -1;
    }

    if (test_sv39_reclaim_empty_tables() != 0) {
        return -1;
    }

    printf("[PASS] Sv39 pure logic\n");
    return 0;
}
