# 用户内存访问：第一批复制函数

状态：2026-10-05 本批验收完成。12 个复制场景和原有宿主回归通过，RV64 构建通过，独立 ASan/UBSan 检查通过（当前环境关闭 leak 检测）。尚未接入 syscall 或在 U-mode 执行复制。

本批先实现独立的用户内存复制函数，并用宿主测试验证。
后续接入 SYSCALL_WRITE，让用户程序打印 Hello from user!。
保留现有共享根页表、静态任务和物理内存恒等映射。

## 接口和行为

新增 include/user_access.h、kernel/user_access.c：

```c
int user_copy_from(
    const Sv39PageTable *root,
    void *destination,
    uintptr_t source_address,
    size_t length
);
```

头文件包含 stddef.h、stdint.h、vm.h，使用 include guard。
source_address 是用户虚拟地址；destination 是内核提供的足够大的可写缓冲区。
源和目标不重叠是调用者的前提，不要求处理任意内核目标指针或重叠复制。

- 成功返回 SV39_OK，任意失败统一返回 SV39_ERR_INVALID_ARGUMENT。
- length == 0 直接成功，不检查 root、destination 或 source_address。
- 非零长度先拒绝 NULL root/destination 和 source_address + length - 1 溢出，
  再检查起始与最后一个字节的 Sv39 canonical 地址。
- 对每一页调用 sv39_query_page；查询必须成功，flags 必须同时有 U 和 R。
  只读用户页允许复制，不要求 W；执行专用页（U/X、没有 R）拒绝。
- 逐页复制，只在查询和权限检查通过后读取该页。
- 失败允许已经复制前面合法页的前缀，调用者必须丢弃缓冲区内容。
  后续 write 必须整次复制成功后才输出，不能边复制边打印。
- 不分配/释放物理页，不改源数据或页表，不调用 flush，不写 CSR，不设置 SUM。

页表及其中的 PA 由内核维护，并指向可访问的物理 RAM；这是本阶段的信任前提。
读取经过检查的 PA 使用现有物理内存恒等映射。当前单 hart 的 syscall 路径内
没有并发修改页表；以后多任务地址空间和并发访问需要重新考虑其生命周期。

## 循环骨架

先完成上述参数校验，再写循环。sv39_query_page 返回的 PA 已带页内偏移，不能重复加偏移。

```c
unsigned char *output = destination;
size_t copied = 0;
while (copied < length) {
    uintptr_t current_address = source_address + copied;
    uint64_t physical_address = 0;
    uint64_t flags = 0;

    /* 查询 current_address；失败或缺少 U/R 就返回错误。 */

    size_t page_remaining = SV39_PAGE_SIZE -
        (current_address & (SV39_PAGE_SIZE - 1U));
    size_t chunk = length - copied;
    if (chunk > page_remaining) {
        chunk = page_remaining;
    }

    /* 从 (const unsigned char *)(uintptr_t)physical_address
       逐字节复制 chunk 个字节到 output + copied。 */

    copied += chunk;
}
return SV39_OK;
```

用 copied 计算每次地址，最后一次不再计算末尾后一个 VA，避免范围恰好到 UINTPTR_MAX 时再次加法溢出。
不使用用户 VA 作为 C 指针。不要调用 memcpy；当前内核没有接入 freestanding libc 实现，
简单字节循环即可。

## 宿主验收

测试函数集中在 test/test_user_access.cpp，fixture 和入口声明在 test/test_user_access.hpp，入口 int test_user_access(void)。
复用 test_support.hpp 的 require_equal/run_case；每个场景使用一个独立活动页池。
源物理页必须来自真实 PMM，不能把不可访问的假 PA 写入页表。

| 场景 | 预期 |
|---|---|
| 单页，VA 从页内非零偏移开始，数据包含零字节 | 成功，逐字节完全一致，不按字符串终止 |
| 两页交界，虚拟页连续、物理页不相邻 | 成功，末尾和下一页开头数据按顺序拼接 |
| 第一页合法，第二页不存在 | 失败，不能访问第二页；允许目标有合法前缀 |
| 第一页合法，第二页没有 U | 失败，源/页表/页数保持不变 |
| 第一页合法，第二页只有 U/X 没有 R | 失败，不因为 X 而允许读取 |
| 起始 VA 未映射 | 失败，不读取源 |
| 非零长度，root 或 destination 为 NULL | 失败 |
| 零长度，root/destination 均 NULL，source 任意 | 成功，无副作用 |
| source = UINTPTR_MAX - 1，length = 4 | 失败，检测整数回绕 |
| source 非 canonical；或起点 canonical、末字节非 canonical | 失败 |
| source = UINTPTR_MAX，length = 1，最后一页具有 U/R 映射 | 成功，不把末字节误当成溢出 |

跨页场景可用 source_address = first_va + SV39_PAGE_SIZE - 4、length = 8，
预期读取第一页末尾 4 字节和第二页开头 4 字节。为保证物理不相邻，
分配 first_page、一个保留的间隔页、second_page，再建立两处相邻 VA 映射；
不要让 PMM 复用间隔页。用 PagePool<8> 足以容纳 root、三个数据页和 L1/L0。
目标缓冲区前后设置哨兵并确认未被覆盖。每个复制调用前后检查空闲页数、
相关映射 PA/flags 和源字节不变。参数失败发生在复制前，目标应不变；
第二页失败场景不要求目标完整保持不变。

已注册上述 12 个场景；NULL 场景包含 root/destination 各自为空及同时为空，
零长度还覆盖非 canonical 地址。状态观察检查源页、物理间隔页、根/L1/L0 表的完整字节、
两处映射、空闲页数、unmap/flush/free 计数和事件数量。
溢出与末字节非 canonical 场景先映射合法起点，确认失败前没有任何目标写入；
边界处的额外映射也核对调用前后 PA/flags。模块入口逐个执行，前例失败不会跳过后例。

接入位置：

1. Makefile 的 OBJS 增加 kernel/user_access.o。
2. test/CMakeLists.txt 的 tinyos_test_kernel 增加 kernel/user_access.c。
3. test/c_api.hpp 的 extern C 块包含 user_access.h。
4. test/test_main.cpp 包含 test_user_access.hpp，调用 test_user_access 并处理失败。
5. 顶层 .cpp 自动加入 CMake，但函数入口仍须显式接入。

## 验证命令

```sh
make test CTEST='ctest --verbose'
make
git diff --check
```

复制函数的纯逻辑由宿主验证；本批没有 syscall 接入，因此不宣称已在 U-mode 实测。
本批已通过；下一批交付当前任务根页表接口、WRITE ABI、
256 字节限额、复制成功后输出及 QEMU 返回值/非法地址验证。
