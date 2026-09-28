#include "test_vm.h"

#include <stdint.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
        "    actual:       %"PRIu64"\n"
        "    expected:     %"PRIu64"\n",
        actual_expression,
        expected_expression,
        file,
        line,
        actual,
        expected
    );

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
    size_t region_size = REGION_PAGES * SV39_PAGE_SIZE;
    void *region = aligned_alloc(SV39_PAGE_SIZE, region_size);
    CHECK(region != NULL);

    if (pmm_init(region, (unsigned char *)region + region_size) != 0) {
        free(region);
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

    free(region);
    return 0;
}

static int test_sv39_mapping_stress(void) {
    enum {
        REGION_PAGES = 64,
        MAPPING_COUNT = 4096
    };

    const size_t region_size = REGION_PAGES * SV39_PAGE_SIZE;

    void* region = aligned_alloc(SV39_PAGE_SIZE, region_size);

    CHECK(region != NULL);

    if (pmm_init(region, (unsigned char*)region + region_size) != 0) {
        free(region);
        return fail("pmm_init for SV39 stress test",
            __FILE__, __LINE__);
    }
    CHECK_EQ(
        pmm_available_pages(),
        REGION_PAGES
    );

    Sv39PageTable* root = sv39_create_page_table();
    if (root == NULL) {
        free(region);
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

        uint64_t actual_physicl_address = 0;
        uint64_t actual_flags = 0;

        CHECK(
            sv39_query_page(root,
                virtual_address,
                &actual_physicl_address,
                &actual_flags
            ) == SV39_OK
        );

        CHECK_EQ(
            actual_physicl_address,
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

    free(region);
    return 0;
}

int test_sv39_corrupted_pte_rejection(void) {
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

    return 0; // 测试通过
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

    printf("[PASS] Sv39 pure logic\n");
    return 0;
}
