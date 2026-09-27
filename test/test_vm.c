#include "test_vm.h"

#include <stdint.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "vm.h"

static int fail(
    const char* msg,
    const char* file,
    unsigned long line
) {
    fprintf(stderr,"[FAIL] %s at %s:%lu\n", msg, file, line);
    return -1;
}

#define CHECK(cond) \
    do {if (!(cond)) { return fail(#cond, __FILE__, __LINE__); } }while(0)

#define CHECK_EQ(a,b) CHECK((uint64_t)(a) == (uint64_t)(b))

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
        uint64_t randon_bits =
            ((uint64_t)next_random(&state) << 32);
        uint64_t ppn = randon_bits &
            ((UINT64_C(1) << 44) - 1);
        uint64_t pa = ppn << 12;
        uint64_t flags = next_random(&state) & SV39_PTE_FLAG_MASK;

        Sv39Pte pte = sv39_make_pte(pa, flags);
        CHECK_EQ(sv39_pte_physical_address(pte), pa);
        CHECK_EQ(sv39_pte_flags(pte), flags);
    }

    return 0;
}

int test_vm(void) {
    if (test_size() != 0) return -1;
    if (test_canonical() != 0) return -1;
    if (test_vpn_index() != 0) return -1;
    if (test_pte_roundtrip() != 0) return -1;
    if (test_validity_and_leaf() != 0) return -1;
    if (test_stress() != 0) return -1;

    printf("[PASS] Sv39 pure logic\n");
    return 0;
}
