#include "drivers/virtio_block.hpp"

/*
// =============== 简要说明 ================
// 查 0x74726976（"virt" 小端序）
// 版本号 2、设备类型 2（块设备）
// 然后往 Status 写 0 等它清零
// 再依次写 ACKNOWLEDGE(1) 和 DRIVER(2)
// 读 DeviceFeatures 挑出自己认识的位写回 DriverFeatures
// 再置 FEATURES_OK(8) 并回读确认
// Descriptor Table (QueueDesc)   驱动写 / 设备读
// Available Ring   (QueueDriver) 驱动写 / 设备读
// Used Ring        (QueueDevice) 设备写 / 驱动读
// 把 QueueReady 置 1 最后把 Status 置 DRIVER_OK(4)
*/

/*
 * ===================== VirtIO 的“存储器初始化” =====================
 *
 * split virtqueue 不是普通函数局部变量，而是 CPU 和设备共同访问的
 * 共享内存。一次读请求至少需要：
 *   - descriptor table：描述三块 buffer 的地址/长度/方向；
 *   - available ring：驱动告诉设备“哪些描述符可用”；
 *   - used ring：设备告诉驱动“哪些请求完成”；
 *   - 请求头、512 字节数据区、1 字节状态：请求私有 DMA 区。
 *
 * 这些对象必须满足：
 *   1. 地址稳定：设备可能在本函数返回后继续 DMA，不能放在栈上；
 *   2. 设备持有请求期间，驱动保留请求占用的内存，确认设备归还后才复用；
 *   3. 对齐正确：descriptor 16B、avail 2B、used 4B；
 *   4. 初始状态确定：avail.idx / used.idx 必须从 0 开始；
 *   5. 设备能访问：写入 MMIO 的地址必须是 guest physical address。
 *
 * 下面的三个队列区都声明为 static，初始为零并放进 .bss：
 *   - static 保证地址固定、生命周期为整个内核；
 *   - .bss 不占 ELF 文件空间，由 entry.S 在 kernel_main 前清零；
 *   - 当前内核根页表对 .bss 做恒等映射，所以 &object 的数值
 *     同时就是 guest physical address，可以直接写给设备。
 *
 * 如果以后内核迁到高半区（VA != PA），下面所有写给 QueueDesc/
 * QueueDriver/QueueDevice 的地址都必须经过 virt_to_phys() 转换，
 * 不能直接把 C++ 指针的数值交给设备。
 * ===================================================================
 */
#define MAGIC_VALUE             UINT64_C(0x74726976)
#define VERSION                 UINT64_C(0x2)
#define DEVICE_ID               UINT64_C(0x2)

#define ACKNOWLEDGE             UINT64_C(0x1)
#define DRIVER                  UINT64_C(0x2)

// 描述符表、可用环和已用环各有 8 个槽。
constexpr std::uint32_t kQueueSize = 8;

// virtio_block_init 保存设备寄存器的基址，提交请求时用它写 QueueNotify。
static std::uintptr_t g_mmio_base{};
// virtio_block_init 保存设备报告的扇区总数，读写入口用它检查扇区编号。
static std::uint64_t g_sector_count{};
// 设备配置完成后允许提交。驱动无法确认请求归还时禁止后续提交。
static bool g_ready{false};
// 设备提供的 RO 位只限制写请求。
static bool g_read_only{false};

/*
 * 驱动填写每个描述符，QEMU 读 address 和 length 来定位本段请求内存。
 * 驱动设置 WRITE 来授权 QEMU 写本段，设置 NEXT 来让 QEMU 继续读 next 指定的描述符。
 * address 使用客体物理地址，当前内核恒等映射使它和驱动指针的数值相同。
 */
struct Descriptor {
    std::uint64_t address;
    std::uint32_t length;
    std::uint16_t flags;
    std::uint16_t next;
};

/*
 * 驱动向可用环的 heads[] 放入待处理请求的描述符链头编号。
 * 驱动推进 index 来发布新槽，QEMU 读 index 后从 heads[] 取出链头。
 */
struct AvailableRing {
    std::uint16_t flags;
    std::uint16_t index;
    std::uint16_t heads[kQueueSize];
    std::uint16_t used_event;
};

/* QEMU 用 id 归还描述符链头，用 length 报告设备写入的字节数。 */
struct UsedElement {
    std::uint32_t id;
    std::uint32_t length;
};

/*
 * QEMU 先向已用环的 elements[] 写一条完成记录，再推进 index。
 * 驱动轮询 index，看到 QEMU 发布新记录后才读取对应的 elements[] 槽。
 */
struct UsedRing {
    std::uint16_t flags;
    std::uint16_t index;
    UsedElement elements[kQueueSize];
    std::uint16_t available_event;
};

static_assert(sizeof(Descriptor) == 16);
static_assert(sizeof(UsedElement) == 8);

/*
 * virtio_block_init 通过 QueueDesc、QueueDriver 和 QueueDevice 分别交给 QEMU
 * 这三个对象的物理基址。驱动静态持有它们，因为 QEMU 会在提交后通过基址访问
 * 共享内存。kernel/boot/entry.S 的 _start 在启动时清零这些对象。
 */
alignas(16) static Descriptor g_descriptors[kQueueSize]{}; // 下标是描述符编号。
alignas(2) static AvailableRing g_available{}; // heads[] 的下标是驱动的提交槽号。
alignas(4) static UsedRing g_used{}; // elements[] 的下标是 QEMU 的完成槽号。

namespace tinyos::drivers {

static std::uint32_t mmio_read32(
    std::uintptr_t base, std::uintptr_t offset
) noexcept {
    return *reinterpret_cast<const volatile std::uint32_t*>(base + offset);
}

static void mmio_write32(
    std::uintptr_t base, std::uintptr_t offset, std::uint32_t value
) noexcept {
    *reinterpret_cast<volatile std::uint32_t*>(base + offset) = value;
}

bool try_mmio_write32(
    std::uintptr_t base , std::uintptr_t offset,
    std::uint32_t value, std::uint32_t requested_value
) noexcept {
    mmio_write32(base, offset, value);

    if ((mmio_read32(base, offset) & requested_value) == 0) {
        return false;
    }

    return true;
}

int virtio_block_init(
    std::uintptr_t mmio_base, std::uint64_t &sector_count
) noexcept {

    // 设备探测：检查魔数 ("virt")、规范版本 (Modern 为 2) 以及目标设备类型 ID
    if (mmio_read32(mmio_base, 0x000) != MAGIC_VALUE ||
        mmio_read32(mmio_base, 0x004) != VERSION ||
        mmio_read32(mmio_base, 0x008) != DEVICE_ID) {
        return -1;
    }

    constexpr std::uintptr_t kStatusOffset = 0x070;
    constexpr unsigned kResetPollLimit = 100'000;

    // 重置设备：向 Status 寄存器写入 0 触发硬件重置
    mmio_write32(mmio_base, kStatusOffset, 0);

    // VirtIO 1.0+ 强制要求：写入 0 后必须轮询等待 Status 真正变为 0，才算重置完成
    unsigned polls = 0;
    while (mmio_read32(mmio_base, kStatusOffset) != 0) {
        ++polls;
        if (polls == kResetPollLimit) {
            return -1;
        }
    }

    // 通知设备：操作系统已识别到这是一个合法的 VirtIO 设备 (ACKNOWLEDGE = 1)
    std::uint32_t status = ACKNOWLEDGE;
    mmio_write32(mmio_base, kStatusOffset, status);

    // 通知设备：操作系统已找到并加载了对应的驱动程序 (DRIVER = 2)
    status |= DRIVER;
    mmio_write32(mmio_base, kStatusOffset, status);

    constexpr std::uintptr_t kDeviceFeatures    = 0x010; // 只读：设备支持的特性位图（当前选中页的 32 位）
    constexpr std::uintptr_t kDeviceFeaturesSel = 0x014; // 只写：选择要读取设备特性的哪一页（0 = 低32位，1 = 高32位）
    constexpr std::uintptr_t kDriverFeatures    = 0x020; // 只写：驱动最终决定启用的特性位图（当前选中页的 32 位）
    constexpr std::uintptr_t kDriverFeaturesSel = 0x024; // 只写：选择要写入驱动特性的哪一页（0 = 低32位，1 = 高32位）

    constexpr std::uint32_t kVersion1OnPage1 = 1U;      // 第 1 页的 bit 0（即全局第 32 位）：VIRTIO_F_VERSION_1，标识支持现代 1.0+ 规范
    constexpr std::uint32_t kReadOnlyOnPage0 = 1U << 5; // 第 0 页的 bit 5：VIRTIO_BLK_F_RO，标识这是一块只读磁盘（说明你在写块设备驱动！）
    constexpr std::uint32_t kFeaturesOk      = 1U << 3; // Status 寄存器(0x070)的 bit 3（值=8）：声明特性协商完成
    constexpr std::uint32_t kFailed          = 1U << 7; // Status 寄存器(0x070)的 bit 7（值=128）：驱动初始化彻底失败时上报的错误标志

    // 第一页提供 VERSION_1, 告诉设备驱动已接管
    // 写给设备
    mmio_write32(mmio_base, kDeviceFeaturesSel, 1);
    // 检验写入VERSION = 1
    if ((mmio_read32(mmio_base, kDeviceFeatures) & kVersion1OnPage1) == 0) {
        mmio_write32(mmio_base, kStatusOffset, status | kFailed);
        return -1;
    }
    // 写给驱动
    mmio_write32(mmio_base, kDriverFeaturesSel, 1);
    mmio_write32(mmio_base, kDriverFeatures, kVersion1OnPage1);

    // 第0页只接受设备实际提供的 RO 位, 检查设备有没有提供它
    // 写入
    mmio_write32(mmio_base, kDeviceFeaturesSel, 0);
    const std::uint32_t accepted_page0 =
        mmio_read32(mmio_base, kDeviceFeatures) & kReadOnlyOnPage0;
    mmio_write32(mmio_base, kDriverFeaturesSel, 0);
    mmio_write32(mmio_base, kDriverFeatures, accepted_page0);

    status |= kFeaturesOk;
    mmio_write32(mmio_base, kStatusOffset, status);

    // 检查
    const std::uint32_t status_after_features =
        mmio_read32(mmio_base, kStatusOffset);
    if ((status_after_features & kFeaturesOk) == 0) {
        mmio_write32(mmio_base, kStatusOffset, status_after_features | kFailed);
        return -1;
    }

    // 设置地址
    constexpr std::uintptr_t QueueSel = 0x030;
    constexpr std::uintptr_t QueueNumMax = 0x034;
    constexpr std::uintptr_t QueueNum = 0x038;
    constexpr std::uintptr_t QueueReady = 0x044;
    constexpr std::uintptr_t QueueDescLow = 0x080;
    constexpr std::uintptr_t QueueDescHigh = 0x084;
    constexpr std::uintptr_t QueueDriverlLow = 0x090;
    constexpr std::uintptr_t QueueDriverHigh = 0x094;
    constexpr std::uintptr_t QueueDeviceLow = 0x0a0;
    constexpr std::uintptr_t QueueDeviceHigh = 0x0a4;

    mmio_write32(mmio_base, QueueSel, 0);

    if (mmio_read32(mmio_base, QueueReady) != 0 ||
        mmio_read32(mmio_base, QueueNumMax) < kQueueSize) {
        mmio_write32(mmio_base, kStatusOffset, status_after_features | kFailed);
        return -1;
    }

    mmio_write32(mmio_base, QueueNum, kQueueSize);

    /*
     * 这里把内核指针值直接写给设备的 QueueDesc 地址寄存器：
     * 仅因为当前内核根页表对 .bss 做了恒等映射，
     * 所以 reinterpret_cast<uintptr_t>(&object) == guest physical address。
     * 如果将来内核迁到高半区，这里必须改成 virt_to_phys(&object)。
     */
    const auto descriptor_address =
        reinterpret_cast<std::uintptr_t>(g_descriptors);
    mmio_write32(mmio_base, QueueDescLow,
                static_cast<std::uint32_t>(descriptor_address));
    mmio_write32(mmio_base, QueueDescHigh,
             static_cast<std::uint32_t>(descriptor_address >> 32));

    const auto avaliable_address =
        reinterpret_cast<std::uintptr_t>(&g_available);    
    mmio_write32(mmio_base, QueueDriverlLow,
            static_cast<std::uint32_t>(avaliable_address));
    mmio_write32(mmio_base, QueueDriverHigh,
            static_cast<std::uint32_t>(avaliable_address >> 32));


    const auto used_address =
        reinterpret_cast<std::uintptr_t>(&g_used);
    mmio_write32(mmio_base, QueueDeviceLow,
            static_cast<std::uint32_t>(used_address));
    mmio_write32(mmio_base, QueueDeviceHigh,
            static_cast<std::uint32_t>(used_address >> 32));

    mmio_write32(mmio_base, QueueReady, 1);

    constexpr std::uint32_t kDriverOk = 1U << 2; // Status bit 2 (值=4)
    constexpr std::uintptr_t kConfigCapacityLow  = 0x100;
    constexpr std::uintptr_t kConfigCapacityHigh = 0x104;
    constexpr std::uintptr_t kConfigGeneration = 0x0fc;
    constexpr unsigned kCapacityReadAttempts = 16;

    for (unsigned attempt = 0; attempt < kCapacityReadAttempts; ++attempt) {
        const auto generation = mmio_read32(mmio_base, kConfigGeneration);
        const std::uint64_t low = mmio_read32(mmio_base, kConfigCapacityLow);
        const std::uint64_t high = mmio_read32(mmio_base, kConfigCapacityHigh);
        if (generation != mmio_read32(mmio_base, kConfigGeneration)) {
            continue;
        }

        const std::uint64_t capacity = low | (high << 32);
        mmio_write32(mmio_base, kStatusOffset, status_after_features | kDriverOk);

        g_mmio_base = mmio_base;
        g_sector_count = capacity;
        g_read_only = (accepted_page0 & kReadOnlyOnPage0) != 0;
        g_ready = true;
        sector_count = capacity;
        return 0;
    }

    mmio_write32(mmio_base, kStatusOffset, status_after_features | kFailed);
    return -1;
}

struct alignas(16) VirtioBlkReqHeader {
    std::uint32_t type;     // 0 = READ (IN), 1 = WRITE (OUT)
    std::uint32_t reserved; // 必须为 0
    std::uint64_t sector;   // 起始扇区号
};

constexpr std::uint16_t VIRTQ_DESC_F_NEXT   = 1;
constexpr std::uint16_t VIRTQ_DESC_F_WRITE  = 2;
constexpr std::uintptr_t QueueNotify        = 0x050;
constexpr std::size_t kSectorSize = 512;

enum class BlockRequestType : std::uint32_t {
    In = 0,
    Out = 1,
};

static unsigned char g_dma_buffer[kSectorSize]{};

// 前置条件：入口已校验参数，当前使用内核根页表，队列没有其他在途请求。
static int submit_block_request(std::uint64_t sector, BlockRequestType type) noexcept {
    // 驱动持有这三块静态 DMA 内存。即使轮询超时并返回，QEMU 仍可能访问它们。
    static VirtioBlkReqHeader req{};
    static volatile std::uint8_t status_byte = 0xFF;

    const bool is_read = type == BlockRequestType::In;
    req.type = static_cast<std::uint32_t>(type);
    req.reserved = 0;
    req.sector = sector;

    // 内核恒等映射使这些指针的数值可直接作为客体物理地址。
    g_descriptors[0].address = reinterpret_cast<std::uintptr_t>(&req);
    g_descriptors[1].address = reinterpret_cast<std::uintptr_t>(g_dma_buffer);
    g_descriptors[2].address = reinterpret_cast<std::uintptr_t>(&status_byte);

    g_descriptors[0].length = sizeof(req);
    g_descriptors[0].flags = VIRTQ_DESC_F_NEXT;
    g_descriptors[0].next = 1;

    // WRITE 表示 QEMU 可以写数据区。读盘时设备写数据区，写盘时设备读数据区。
    g_descriptors[1].length = kSectorSize;
    g_descriptors[1].flags = is_read
        ? (VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE)
        : VIRTQ_DESC_F_NEXT;
    g_descriptors[1].next = 2;

    // QEMU 向描述符 2 对应的 status_byte 写本次请求结果。驱动省略 NEXT 以结束描述符链。
    g_descriptors[2].length = sizeof(status_byte);
    g_descriptors[2].flags = VIRTQ_DESC_F_WRITE;
    g_descriptors[2].next = 0;

    // 驱动每次提交前用 0xff 标记状态字节尚待设备写回。
    status_byte = 0xff;

    // 驱动用提交计数取模来选可用环的槽。右边的 0 是描述符链头编号，QEMU 会从该项开始读链。
    g_available.heads[g_available.index % kQueueSize] = 0;

    // 驱动先让描述符和 heads 槽对 QEMU 可见，再推进提交计数，正式交出这条请求。
    asm volatile("fence iorw, iorw" ::: "memory");

    ++g_available.index;
    // 驱动保证新 index 先于 MMIO 通知可见，然后通知 QEMU 检查队列 0。
    asm volatile("fence iorw, iorw" ::: "memory");

    mmio_write32(g_mmio_base, QueueNotify, 0);

    // last_used_index 是驱动的消费计数，指向下一条待读的完成项。
    // g_used.index 由 QEMU 推进。volatile 引用让驱动每轮都重新读取这个共享字段。
    static std::uint16_t last_used_index = 0;
    const volatile std::uint16_t& device_index = g_used.index;

    // 驱动有限次轮询 QEMU 的完成计数。两边计数相等时，QEMU 尚未发布新的完成项。
    for (unsigned attempt = 0; attempt < 100'000; ++attempt) {
        if (device_index != last_used_index) {
            break;
        }
    }

    // 此时驱动只等一条请求，QEMU 的 16 位完成计数应恰好加一，溢出时回到零。
    // 计数没变或跳了多格时，驱动无法确认请求已经归还，因此停止后续提交。
    const std::uint16_t observed_index = device_index;
    const auto expected_index = static_cast<std::uint16_t>(last_used_index + 1U);
    if (observed_index != expected_index) {
        g_ready = false;
        return -1;
    }

    // 驱动先观察 QEMU 发布的新计数，再经屏障读取完成项。旧的消费计数取模才是本项槽号。
    asm volatile("fence iorw, iorw" ::: "memory");
    const UsedElement completion = g_used.elements[last_used_index % kQueueSize];

    // 驱动用 id 核对 QEMU 归还的是刚提交的描述符链 0。归属不明时禁止复用请求内存。
    if (completion.id != 0) {
        g_ready = false;
        return -1;
    }

    // 驱动确认 QEMU 已归还请求后就消费完成项，设备报告失败也要推进这份计数。
    last_used_index = observed_index;

    const std::uint32_t expected_length = is_read
        ? kSectorSize + sizeof(status_byte)
        : sizeof(status_byte);
    if (completion.length != expected_length || status_byte != 0) {
        return -1;
    }

    return 0;
}

int virtio_block_read(
    std::uint64_t sector,
    std::span<unsigned char> destination
) noexcept {
    if (!g_ready || destination.size() != kSectorSize || sector >= g_sector_count) {
        return -1;
    }

    if (submit_block_request(sector, BlockRequestType::In) != 0) {
        return -1;
    }

    // 驱动只在成功时把 DMA 缓冲区复制到调用方的 destination，保留失败时的原内容。
    for (std::size_t i = 0; i < kSectorSize; ++i) {
        destination[i] = g_dma_buffer[i];
    }

    return 0;
}

int virtio_block_write(
    std::uint64_t sector,
    std::span<const unsigned char> source
) noexcept {
    if (!g_ready ||
        g_read_only ||
        source.size() != kSectorSize ||
        sector >= g_sector_count) {
        return -1;
    }

    for (std::size_t i = 0; i < kSectorSize; ++i) {
        g_dma_buffer[i] = source[i];
    }

    return submit_block_request(sector, BlockRequestType::Out);
}
}
