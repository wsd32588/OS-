#include "user/user_elf.h"

/* define | name | offset | width */
/* ========================================================================= */
/*                          64位 ELF 文件头 (Elf64_Ehdr) 偏移定义             */
/* ========================================================================= */

// 1. 文件魔数 (e_ident[0..3])，固定为 0x7F, 'E', 'L', 'F' (0x7F 0x45 0x4C 0x46)
#define ELF_HEADER_MAGIC_OFFSET         0    // 大小: 4 字节，用于验证是否为合法 ELF 文件
// 2. 硬件架构位数 (e_ident[EI_CLASS]): 1 = 32位 (ELFCLASS32), 2 = 64位 (ELFCLASS64)
#define ELF_HEADER_EI_CLASS_OFFSET      4    // 大小: 1 字节
// 3. 字节序 (e_ident[EI_DATA]): 1 = 小端序 (ELFDATA2LSB), 2 = 大端序 (ELFDATA2MSB)
#define ELF_HEADER_EI_DATA_OFFSET       5    // 大小: 1 字节
// 4. ELF 规范主版本号 (e_ident[EI_VERSION]): 固定为 1 (EV_CURRENT)
#define ELF_HEADER_EI_VERSION_OFFSET    6    // 大小: 1 字节
/* 注: 偏移 7 ~ 15 为 e_ident 数组的填充字节 (EI_PAD)，通常全为 0x00，占 9 字节 */
// 5. 文件类型 (e_type): 1 = 目标文件(.o), 2 = 可执行文件, 3 = 共享库(.so), 4 = 核心转储(core)
#define ELF_HEADER_E_TYPE_OFFSET        16   // 大小: 2 字节 (uint16_t)
// 6. 目标 CPU 指令集架构 (e_machine): 如 0x3E(62)=x86_64, 0xF3(243)=RISC-V, 0xB7(183)=AArch64
#define ELF_HEADER_E_MACHINE_OFFSET     18   // 大小: 2 字节 (uint16_t)
// 7. ELF 内部对象文件版本 (e_version): 对应 EV_CURRENT (1)
#define ELF_HEADER_E_VERSION_OFFSET     20   // 大小: 4 字节 (uint32_t)
// 8. 程序执行入口虚拟地址 (e_entry): 进程启动跳转执行的第一条指令地址 (如 _start)
#define ELF_HEADER_E_ENTRY_OFFSET       24   // 大小: 8 字节 (uint64_t)
// 9. 程序头表在文件内的起始字节偏移 (e_phoff - Program Header Offset)
#define ELF_HEADER_E_PHOFF_OFFSET       32   // 大小: 8 字节 (uint64_t)
/* 注: 偏移 40 ~ 47 通常为节头表偏移 (e_shoff, 8字节)；偏移 48 ~ 51 为处理器特定标志 (e_flags, 4字节) */
// 10. ELF 文件头自身大小 (e_ehsize): 64位 ELF 下固定为 64 (0x40) 字节
#define ELF_HEADER_E_EHSIZE_OFFSET      52   // 大小: 2 字节 (uint16_t)
// 11. 程序头表单个条目大小 (e_phentsize): 64位 ELF 下每个 Elf64_Phdr 占 56 (0x38) 字节
#define ELF_HEADER_E_PHENTSIZE_OFFSET   54   // 大小: 2 字节 (uint16_t)
// 12. 程序头表中条目总数 (e_phnum): 记录文件包含多少个需要加载/解析的段 (Segment)
#define ELF_HEADER_E_PHNUM_OFFSET       56   // 大小: 2 字节 (uint16_t)

/*
 * 按字节读小端整数
 */
static uint64_t read_little_endian(const unsigned char* bytes, size_t width) {
    uint64_t value = 0;

    for (size_t i = 0; i < width; ++i) {
        value |= (uint64_t)bytes[i] << (8U * i);
    }

    return value;
}

int user_elf_parse(const void *image, size_t image_size,
    UserElfLayout *layout) {

    if (image == NULL || layout == NULL || image_size < 64U) {
        return USER_ELF_INVALID_ARGUMENT;
    }

    const unsigned char *input = image;

    const unsigned char* maigc = input + ELF_HEADER_MAGIC_OFFSET;

    if (maigc[0] != 0x7f ||
        maigc[1] != 'E' ||
        maigc[2] != 'L' ||
        maigc[3] != 'F') {
        return USER_ELF_INVALID_ARGUMENT;
    }
    /* 确认可以按 ELF64、小端、版本 1 的规则解释后续字段。 */
    if (input[ELF_HEADER_EI_CLASS_OFFSET] != 2U ||
        input[ELF_HEADER_EI_DATA_OFFSET] != 1U ||
        input[ELF_HEADER_EI_VERSION_OFFSET] != 1U) {
        return USER_ELF_INVALID_ARGUMENT;
    }
    /* 本批支持 RISC-V 的 ET_EXEC 可执行文件。 */
    if (read_little_endian(input + ELF_HEADER_E_TYPE_OFFSET, 2) != 2U ||
        read_little_endian(input + ELF_HEADER_E_MACHINE_OFFSET, 2) != 243U ||
        read_little_endian(input + ELF_HEADER_E_VERSION_OFFSET, 4) != 1U) {
        return USER_ELF_INVALID_ARGUMENT;
    }
    /* 确认实际格式与解析器使用的固定尺寸一致。 */
    if (read_little_endian(input + ELF_HEADER_E_EHSIZE_OFFSET, 2) != 64U ||
        read_little_endian(input + ELF_HEADER_E_PHENTSIZE_OFFSET, 2) != 56U) {
        return USER_ELF_INVALID_ARGUMENT;
    }
    uint64_t program_header_offset = read_little_endian(
        input + ELF_HEADER_E_PHOFF_OFFSET, 8);

    uint16_t program_header_count = (uint16_t)read_little_endian(
        input + ELF_HEADER_E_PHNUM_OFFSET, 2);

    if (program_header_count == 0U ||
        program_header_count == UINT16_MAX ||
        program_header_offset > image_size ||
        program_header_count >
            (image_size - program_header_offset) / 56U) {
        return USER_ELF_INVALID_ARGUMENT;
    }

    UserElfLayout parsed = {0};

    parsed.entry_address = read_little_endian(
        input + ELF_HEADER_E_ENTRY_OFFSET, 8);
    parsed.program_header_count = program_header_count;

    for (size_t i = 0; i < program_header_count; ++i) {
        const unsigned char* header =
            input + (size_t)program_header_offset + i * 56U;
        uint32_t type = (uint32_t)read_little_endian(header, 4);

        if (type != 1U) {
            continue;
        }

        if (parsed.load_segment_count >= USER_ELF_MAX_LOAD_SEGMENTS) {
            return USER_ELF_INVALID_ARGUMENT;
        }

        UserElfSegment segment = {
            .file_offset     = read_little_endian(header + 8, 8),
            .virtual_address = read_little_endian(header + 16, 8),
            .file_size       = read_little_endian(header + 32, 8),
            .memory_size     = read_little_endian(header + 40, 8),
            .alignment       = read_little_endian(header + 48, 8),
            .flags           = (uint32_t)read_little_endian(header + 4, 4)
        };

        if (segment.file_size > segment.memory_size ||
            segment.file_offset > image_size ||
            segment.file_size > image_size - segment.file_offset ||
            segment.memory_size > UINT64_MAX - segment.virtual_address
        ) {
            return USER_ELF_INVALID_ARGUMENT;
        }

        if (segment.alignment > 1) {
            if ((segment.alignment & (segment.alignment - 1U)) != 0 ||
                segment.virtual_address % segment.alignment !=
                segment.file_offset % segment.alignment
            ) {
                return USER_ELF_INVALID_ARGUMENT;
            }
        }

        if (parsed.load_segment_count >= USER_ELF_MAX_LOAD_SEGMENTS) {
            return USER_ELF_INVALID_ARGUMENT;
        }

        parsed.segments[parsed.load_segment_count] = segment;
        ++parsed.load_segment_count;
    }

    if (parsed.load_segment_count == 0) {
        return USER_ELF_INVALID_ARGUMENT;
    }

    *layout = parsed;
    return USER_ELF_OK;
}
