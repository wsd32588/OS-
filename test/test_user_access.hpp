#pragma once

#include "c_api.hpp"
#include "user_memory_fixture.hpp"

namespace tinyos_test {
class UserAccessFixture {
    private:
        PagePool<8> pool_;
    public:
        static constexpr uint64_t base_address = UINT64_C(0x00400000);
        static constexpr uint64_t first_address = base_address;
        static constexpr uint64_t gap_address   = base_address + SV39_PAGE_SIZE;
        static constexpr uint64_t second_address = first_address + SV39_PAGE_SIZE;

        static constexpr uint64_t page_flags = SV39_PTE_U | SV39_PTE_R | SV39_PTE_A;

        Sv39PageTable* root = nullptr;
        unsigned char* first_page;
        unsigned char* gap_page;
        unsigned char* second_page;

        UserAccessFixture() {
            root = sv39_create_page_table();
            require(root != nullptr, "alllocate borrowed root oage table");
            first_page = static_cast<unsigned char*>(pmm_alloc_page());
            gap_page = static_cast<unsigned char*>(pmm_alloc_page());
            second_page = static_cast<unsigned char*>(pmm_alloc_page());
            require(first_page != nullptr && gap_page != nullptr && second_page != nullptr,
                "allocate pages");

            require_equal(sv39_map_page(root, first_address,
                reinterpret_cast<uintptr_t>(first_page), page_flags), SV39_OK,
                "first page mapped");
            require_equal(sv39_map_page(root, second_address,
                reinterpret_cast<uintptr_t>(second_page), page_flags), SV39_OK,
                "second page mapped");
        }

    UserAccessFixture(const UserAccessFixture &) = delete;
    UserAccessFixture &operator=(const UserAccessFixture &) = delete;
};
}

int test_user_access(void);
