#include "test_user_image.hpp"
#include "test_support.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {
using namespace tinyos_test;

constexpr unsigned char GUARD = 0xcc;
constexpr std::size_t USE_IMAGE_SIZE = SIZE_MAX;

/*
 * 一行就是一个场景，驱动的 run_scenario 负责准备缓冲区、调用、比较。
 *
 * 目标缓冲区总是先填满 GUARD，段从 destination_offset 开始，末尾留一个
 * 哨兵，所以数组长度由 destination_offset + capacity + 1 推出，不必逐例
 * 手写 std::array。
 *
 * 字段顺序就是 C++20 指定初始化器的书写顺序；省略的字段取默认值。
 * 空 image_size 表示使用 image.size()，空 expected_destination 表示目标
 * 缓冲区必须逐字节保持初始值（全部是 GUARD）。
 */
struct Scenario {
    std::string_view name;

    std::vector<unsigned char> image = {};
    bool null_image = false;
    std::size_t image_size = USE_IMAGE_SIZE;

    bool null_destination = false;
    std::size_t destination_offset = 0;
    std::size_t capacity = 0;

    std::size_t file_offset = 0;
    std::size_t file_size = 0;
    std::size_t memory_size = 0;

    int expected_result = USER_IMAGE_OK;
    std::vector<unsigned char> expected_destination = {};
};

void run_scenario(const Scenario &scenario) {
    const std::size_t buffer_size =
        scenario.null_destination
            ? 0
            : scenario.destination_offset + scenario.capacity + 1;
    std::vector<unsigned char> buffer(buffer_size, GUARD);
    const std::vector<unsigned char> original_buffer = buffer;
    const std::vector<unsigned char> original_image = scenario.image;
    const std::size_t image_size =
        scenario.image_size == USE_IMAGE_SIZE ? scenario.image.size()
                                              : scenario.image_size;

    const int result = user_image_load_segment(
        scenario.null_destination ? nullptr
                                  : buffer.data() + scenario.destination_offset,
        scenario.capacity,
        scenario.null_image ? nullptr : scenario.image.data(),
        image_size, scenario.file_offset, scenario.file_size,
        scenario.memory_size);

    require_equal(result, scenario.expected_result, "load return value");

    if (scenario.expected_result == USER_IMAGE_OK) {
        if (!scenario.null_destination) {
            const std::vector<unsigned char> &expected =
                scenario.expected_destination.empty()
                    ? original_buffer
                    : scenario.expected_destination;
            require_equal(buffer, expected, "segment contents and guards");
        }
        if (!scenario.null_image) {
            require_equal(scenario.image, original_image,
                          "source image preserved");
        }
    } else {
        require_equal(buffer, original_buffer,
                      "failure must not write the destination");
        require_equal(scenario.image, original_image,
                      "failure must not touch the image");
    }
}

const Scenario scenarios[] = {
    /* S1 初值 + 零尾部：NUL 与高位字节必须原样复制，段外保持哨兵。 */
    {
        .name = "user image: S1 initialized bytes and zero tail",
        .image = {0x99, 'A', 0, 0xff, 0x88},
        .destination_offset = 1,
        .capacity = 6,
        .file_offset = 1,
        .file_size = 3,
        .memory_size = 6,
        .expected_destination = {GUARD, 'A', 0, 0xff, 0, 0, 0, GUARD},
    },
    /* S2 file_size == memory_size，恰好填满容量，不做额外清零。 */
    {
        .name = "user image: S2 all initialized bytes fill capacity",
        .image = {0x11, 0x22, 0x33, 0x44},
        .destination_offset = 1,
        .capacity = 4,
        .file_offset = 0,
        .file_size = 4,
        .memory_size = 4,
        .expected_destination = {GUARD, 0x11, 0x22, 0x33, 0x44, GUARD},
    },
    /* S3 纯 BSS：NULL 镜像、image_size = 0，只按 memory_size 清零。 */
    {
        .name = "user image: S3 pure BSS clears the whole segment",
        .null_image = true,
        .destination_offset = 1,
        .capacity = 4,
        .memory_size = 4,
        .expected_destination = {GUARD, 0, 0, 0, 0, GUARD},
    },
    /* S4a 全部指针 NULL、全部长度 0：成功且不读写内存。 */
    {
        .name = "user image: S4a all NULL and zero is legal",
        .null_image = true,
        .null_destination = true,
    },
    /* S4b 非空镜像、offset == image_size、file_size == memory_size == 0。 */
    {
        .name = "user image: S4b empty segment at end of image",
        .image = {0x99, 'A', 0, 0xff, 0x88},
        .destination_offset = 1,
        .capacity = 2,
        .file_offset = 5,
    },
    /* S5 file_size > memory_size。 */
    {
        .name = "user image: S5 file longer than memory",
        .image = {0x99, 'A', 0, 0xff, 0x88},
        .destination_offset = 1,
        .capacity = 6,
        .file_size = 4,
        .memory_size = 3,
        .expected_result = USER_IMAGE_INVALID_ARGUMENT,
    },
    /* S6 memory_size > destination_capacity。 */
    {
        .name = "user image: S6 destination capacity too small",
        .image = {0x99, 'A', 0, 0xff, 0x88},
        .destination_offset = 1,
        .capacity = 2,
        .file_size = 1,
        .memory_size = 3,
        .expected_result = USER_IMAGE_INVALID_ARGUMENT,
    },
    /* S7a offset > image_size。 */
    {
        .name = "user image: S7a file offset beyond image",
        .image = {0x99, 'A', 0, 0xff, 0x88},
        .destination_offset = 1,
        .capacity = 6,
        .file_offset = 6,
        .expected_result = USER_IMAGE_INVALID_ARGUMENT,
    },
    /* S7b offset == image_size 且 file_size = 1（规则 4 的零剩余边界）。 */
    {
        .name = "user image: S7b one byte past end of image",
        .image = {0x99, 'A', 0, 0xff, 0x88},
        .destination_offset = 1,
        .capacity = 6,
        .file_offset = 5,
        .file_size = 1,
        .memory_size = 1,
        .expected_result = USER_IMAGE_INVALID_ARGUMENT,
    },
    /* S7c offset 在镜像内部，但 file_offset + file_size 越过镜像尾。 */
    {
        .name = "user image: S7c file range crosses end of image",
        .image = {0x99, 'A', 0, 0xff, 0x88},
        .destination_offset = 1,
        .capacity = 6,
        .file_offset = 3,
        .file_size = 3,
        .memory_size = 3,
        .expected_result = USER_IMAGE_INVALID_ARGUMENT,
    },
    /* S8a file_offset = SIZE_MAX：必须先被范围检查拦下，不能真去读。 */
    {
        .name = "user image: S8a huge file offset",
        .image = {0x99, 'A', 0, 0xff, 0x88},
        .destination_offset = 1,
        .capacity = 2,
        .file_offset = SIZE_MAX,
        .file_size = 2,
        .memory_size = 2,
        .expected_result = USER_IMAGE_INVALID_ARGUMENT,
    },
    /* S8b file_size == memory_size == SIZE_MAX，真实缓冲区仍然很小。 */
    {
        .name = "user image: S8b huge file and memory size",
        .image = {0x99, 'A', 0, 0xff, 0x88},
        .destination_offset = 1,
        .capacity = 2,
        .file_size = SIZE_MAX,
        .memory_size = SIZE_MAX,
        .expected_result = USER_IMAGE_INVALID_ARGUMENT,
    },
    /* S9a destination == NULL 而 memory_size > 0。 */
    {
        .name = "user image: S9a NULL destination with nonzero size",
        .image = {0x99, 'A', 0, 0xff, 0x88},
        .null_destination = true,
        .capacity = 4,
        .file_size = 1,
        .memory_size = 1,
        .expected_result = USER_IMAGE_INVALID_ARGUMENT,
    },
    /* S9b1 image == NULL 而 image_size > 0。 */
    {
        .name = "user image: S9b1 NULL image with nonzero image size",
        .null_image = true,
        .image_size = 5,
        .destination_offset = 1,
        .capacity = 6,
        .file_size = 1,
        .memory_size = 1,
        .expected_result = USER_IMAGE_INVALID_ARGUMENT,
    },
    /* S9b2 零长度也要检查 NULL 镜像，不能被特判放过。 */
    {
        .name = "user image: S9b2 NULL image with zero lengths",
        .null_image = true,
        .image_size = 5,
        .destination_offset = 1,
        .capacity = 6,
        .expected_result = USER_IMAGE_INVALID_ARGUMENT,
    },
    /* S10 memory_size == file_size == 0，但 offset > image_size。 */
    {
        .name = "user image: S10 zero length with illegal offset",
        .image = {0x99, 'A', 0, 0xff, 0x88},
        .destination_offset = 1,
        .capacity = 6,
        .file_offset = 6,
        .expected_result = USER_IMAGE_INVALID_ARGUMENT,
    },
};

} // namespace

int test_user_image(void) {
    bool passed = true;
    // 先 run_case 再累加，前面的场景失败不跳过后面的场景。
    for (const Scenario &scenario : scenarios) {
        passed =
            run_case(scenario.name, [&] { run_scenario(scenario); }) && passed;
    }
    return passed ? 0 : -1;
}
