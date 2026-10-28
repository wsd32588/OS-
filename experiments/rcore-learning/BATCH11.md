# BATCH11：地址空间概念、地址类型与 Sv39 位拆分

2026-10-18：作者已完成六处实现并接入mm::init，功能验收通过。
实际make run无警告构建、三条地址日志与原应用抢占/退出正常，QEMU主动关机退出0。
以下保留整批要求与原始骨架；第9节三题已集中复核，第三题已补充讲解。

## 1. 当前位置与目标

第四章路线：内核堆（BATCH10已完成）→地址空间概念→Sv39地址格式（本批）
→物理页帧分配→页表构造与查询→内核/应用地址空间→分页下的任务运行。

本批对应教材“地址空间”与“SV39多级页表的硬件机制”开头的地址表示部分。
完成后能用四个类型区分字节地址和页号，得到页内偏移、三级索引，并正确处理高位地址。
这仍是纯数值计算：satp继续Bare，不分配物理页、不读写页表、不改变应用地址或调度。

## 2. 先理解地址空间要解决什么

当前两个应用分别链接/装载到0x80400000和0x80420000，内存访问没有经过分页翻译。
下一步希望两个应用可以使用相同的虚拟地址，却分别访问属于自己的物理页：

```text
任务A的VA 0x1000 ── A的映射 ──→ 物理页甲
任务B的VA 0x1000 ── B的映射 ──→ 物理页乙
```

地址空间包括映射关系与访问权限；只有把类型命名为VirtAddr，并不会建立这种关系。
在后续分页模式下，硬件根据当前页表与权限进行翻译；本批先把输入数字表示清楚。

| 类型 | 含义 | 本批例子 |
| --- | --- | --- |
| VirtAddr，VA | 虚拟字节地址 | 0x80401234 |
| VirtPageNum，VPN | 虚拟页号，不含页内偏移 | 0x80401 |
| PhysAddr，PA | 物理字节地址 | 假设为0x81234234 |
| PhysPageNum，PPN | 物理页号，不含页内偏移 | 对应0x81234 |

假设之后页表把VPN 0x80401映射到PPN 0x81234，则访问VA 0x80401234会得到PA 0x81234234。
这是示意映射，不是当前内核已经安装的映射。类型转换不能自行得出这个PPN。
4KiB页的页内偏移0x234保持不变；VPN与PPN之间的关系必须通过映射查找。

## 3. Sv39的位布局

本项目为RV64，usize是64位。Sv39使用39位虚拟地址信息，4KiB=2^12，所以：

```text
完整VA： [63........39][38.....30][29.....21][20.....12][11......0]
          bit38的重复     VPN[2]     VPN[1]     VPN[0]     offset
                           9位        9位        9位        12位

VPN：27位，不含offset
indexes()返回：[VPN[2], VPN[1], VPN[0]]，按未来从根到叶的遍历顺序
```

每级9位可选512项；后续每个PTE为8字节，因此一张512项的页表正好占4KiB。
此处只学习4KiB叶映射，Sv39支持的大页留到相关实现时讨论。

合法的完整Sv39 VA必须满足bits[63:39]全都等于bit38，即canonical（规范形式）：

| 例子 | 是否满足Sv39格式 |
| --- | --- |
| 0x0000000000000000..0x0000003fffffffff | 是，低半区 |
| 0xffffffc000000000..0xffffffffffffffff | 是，高半区 |
| 0x0000004000000000 | 否，bit38为1而高位未扩展 |
| 0x0000008000000000 | 否，高位与bit38不一致 |

格式合法不保证存在映射或拥有访问权限。当前Bare模式也不会因为我们调用检查函数而开启Sv39。
本批PA表示最多56位、PPN最多44位的基础Sv39编码范围；这不表示机器真的安装了2^56字节RAM。
当前QEMU实际RAM仍为0x80000000..0x88000000，固件等区域仍有各自访问限制。

## 4. 表示约定与模块关系

创建os/src/mm/address.rs，定义四个小包装类型。字段私有，外部通过new校验后创建，
通过as_usize取出数值。这样避免把PA、VA、PPN、VPN随意混用；type别名做不到这种区分。

- VirtAddr内部保存完整64位canonical地址，不自动把非法地址截短成另一个地址。
- VirtPageNum只保存低27位页号；从高半区VA取VPN时要排除符号扩展位。
- VPN还原VA时补回12个零偏移，再依据bit38扩展高位。
- PhysAddr/PhysPageNum分别保存56/44位范围内的数值。
- new返回Option，非法输入得到None；不用assert来接收外部错误输入。
- from类型转换、迭代器、泛型、裸指针解引用都不是本批要求。

与固定官方代码的差异：官方From<usize>常用mask截断，并在VirtAddr转回usize时符号扩展。
本批采用明确检查的new和完整VA表示；因此不能照抄官方内部self.0的处理而忽略表示差异。
这些包装类型只约束数值，不证明对应内存存在、可访问或由调用者独占。

## 5. 完整骨架：作者填写六处TODO

下面是一份address.rs骨架。六处都属于同一个概念：按位解释地址，核心合计只需少量代码。
完成后移除compile_error，保留构造检查与run_demo。作者现已创建并完成实际文件，以下为原始骨架。

```rust
pub const PAGE_SIZE_BITS: usize = 12;
pub const PAGE_SIZE: usize = 1 << PAGE_SIZE_BITS;
const VA_BITS: usize = 39;
const VA_MASK: usize = (1usize << VA_BITS) - 1;
const VPN_BITS: usize = VA_BITS - PAGE_SIZE_BITS;
const PA_BITS: usize = 56;
const PPN_BITS: usize = PA_BITS - PAGE_SIZE_BITS;
const INDEX_MASK: usize = (1usize << 9) - 1;

const _: () = assert!(usize::BITS == 64);
compile_error!("BATCH11 address helpers are unfinished; read BATCH11.md");

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PhysAddr(usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct VirtAddr(usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PhysPageNum(usize);
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct VirtPageNum(usize);

pub fn is_sv39_canonical(raw: usize) -> bool {
    // A: bits[63:39] must all equal bit38.
    todo!("validate the full Sv39 address");
}

fn page_offset(raw: usize) -> usize {
    // B: retain the lowest PAGE_SIZE_BITS bits.
    todo!("extract the page offset");
}

impl PhysAddr {
    pub fn new(raw: usize) -> Option<Self> {
        if raw < (1usize << PA_BITS) { Some(Self(raw)) } else { None }
    }
    pub fn as_usize(self) -> usize { self.0 }
    pub fn page_offset(self) -> usize { page_offset(self.0) }
    pub fn floor(self) -> PhysPageNum {
        // C: remove the byte offset. The constructor already checked PA width.
        todo!("get the physical page number");
    }
}

impl VirtAddr {
    pub fn new(raw: usize) -> Option<Self> {
        if is_sv39_canonical(raw) { Some(Self(raw)) } else { None }
    }
    pub fn as_usize(self) -> usize { self.0 }
    pub fn page_offset(self) -> usize { page_offset(self.0) }
    pub fn floor(self) -> VirtPageNum {
        // D: exclude sign-extension bits, then remove the offset: 27-bit VPN.
        todo!("get the virtual page number");
    }
}

impl PhysPageNum {
    pub fn new(raw: usize) -> Option<Self> {
        if raw < (1usize << PPN_BITS) { Some(Self(raw)) } else { None }
    }
    pub fn as_usize(self) -> usize { self.0 }
    pub fn to_addr(self) -> PhysAddr { PhysAddr(self.0 << PAGE_SIZE_BITS) }
}

impl VirtPageNum {
    pub fn new(raw: usize) -> Option<Self> {
        if raw < (1usize << VPN_BITS) { Some(Self(raw)) } else { None }
    }
    pub fn as_usize(self) -> usize { self.0 }
    pub fn to_addr(self) -> VirtAddr {
        // E: add a zero offset, then sign-extend bit38 to form a full VA.
        todo!("restore the canonical page start");
    }
    pub fn indexes(self) -> [usize; 3] {
        // F: highest nine bits first: [VPN[2], VPN[1], VPN[0]].
        todo!("split the three page-table indexes");
    }
}

pub fn run_demo() {
    let va = VirtAddr::new(0x8040_1234).expect("valid demo VA");
    let vpn = va.floor();
    let pa = PhysAddr::new(0x8123_4234).expect("valid demo PA");
    let ppn = pa.floor();
    assert_eq!(PAGE_SIZE, 4096);
    assert_eq!(vpn.as_usize(), 0x80401);
    assert_eq!(va.page_offset(), 0x234);
    assert_eq!(vpn.indexes(), [2, 2, 1]);
    assert_eq!(vpn.to_addr().as_usize(), 0x8040_1000);
    assert_eq!(ppn.as_usize(), 0x81234);
    assert_eq!(pa.page_offset(), 0x234);
    assert_eq!(ppn.to_addr().as_usize(), 0x8123_4000);
    crate::println!("[addr] va={:#x} vpn={:#x} indexes={:?} offset={:#x}",
        va.as_usize(), vpn.as_usize(), vpn.indexes(), va.page_offset());

    let high = VirtAddr::new(0xffff_ffc0_0000_1234).expect("valid high VA");
    assert_eq!(high.floor().as_usize(), 0x4000001);
    assert_eq!(high.floor().indexes(), [256, 0, 1]);
    assert_eq!(high.page_offset(), 0x234);
    assert_eq!(high.floor().to_addr().as_usize(), 0xffff_ffc0_0000_1000);
    crate::println!("[addr] high={:#x} page_start={:#x}",
        high.as_usize(), high.floor().to_addr().as_usize());

    assert_eq!(VirtPageNum::new(vpn.as_usize()), Some(vpn));
    assert_eq!(PhysPageNum::new(ppn.as_usize()), Some(ppn));
    assert!(PhysAddr::new(1usize << PA_BITS).is_none());
    assert!(PhysPageNum::new(1usize << PPN_BITS).is_none());
    assert!(VirtPageNum::new(1usize << VPN_BITS).is_none());
    assert!(VirtAddr::new(0x0000_0040_0000_0000).is_none());
    crate::println!("[addr] invalid Sv39 address rejected");
}
```

expect/assert只用于这里固定的演示常量与程序不变量；实际new接口用Option报告非法输入。
新增类型使用self按值传参，因为它们只有一个usize且实现Copy，不需要引入借用管理。

## 6. 六处实现要求与常见误区

| 完成点 | 算法与要求 |
| --- | --- |
| A canonical | 取bit38；为0则raw右移39后的值必须是0，为1则必须全是1。注意“高位全1”的比较值也要右移39 |
| B offset | 用PAGE_SIZE-1作为低12位掩码；不能用除法得到页号后当作偏移 |
| C PA floor | 将已验证PA右移12位，得到PPN |
| D VA floor | 先排除高位扩展位再右移12位，得到低27位VPN |
| E VPN to_addr | 左移12位得到低39位页首；bit38为1时将bits[63:39]补1，为0时保持0 |
| F indexes | VPN分别右移18、9、0位，每次取低9位，返回根→叶顺序 |

可以用普通if和位运算，不需要unsafe、汇编、宏生成或泛型。
VPN有27位，单级索引只有9位；把整个VPN当作一张页表的下标会越界。
floor得到的是页号，不是页首字节地址；需要to_addr才能得到对应页首。
地址在页内的任意位置都可以floor，不要求预先对齐。

本批刻意暂不加入ceil/页范围迭代。稍后处理半开区间时，结束页号可能是边界哨兵，
不能默认总能还原成可访问地址；尤其低半区末尾可能跨入非canonical洞，
最高地址向上取整也可能溢出。后续会在明确范围语义后处理，不用直接加4095掩盖问题。

## 7. 接线和可观察路径

只新增address.rs，并在os/src/mm/mod.rs完成两处接线：

```rust
pub mod address;
pub mod heap_allocator;

pub fn init() -> Result<(), &'static str> {
    heap_allocator::init_heap()?;
    heap_allocator::run_demo();
    address::run_demo();
    Ok(())
}
```

```text
clear_bss → trap::init → mm::init
  → 原heap初始化/演示/释放
  → 地址类型构造 → floor/offset/indexes → 高VA页首还原 → 非法输入拒绝
  → 原应用加载/任务初始化/timer/双应用运行与退出
```

main.rs、Cargo依赖、链接脚本、用户程序、trap和调度器都不需要为本批改动。
不要将这些软件检查误接成satp写入，也不要照搬官方address.rs中的get_mut、
get_pte_array等直接构造引用的方法；页号本身不具备解引用所需的安全保证。

## 8. 完成标准与必要验证

填完六处TODO、移除本批compile_error，按原命令运行：

```sh
cd /home/asus/tinyos/experiments/rcore-learning
make build
timeout 8s make run
echo $?
```

原heap日志之后、应用装载之前应出现：

```text
[addr] va=0x80401234 vpn=0x80401 indexes=[2, 2, 1] offset=0x234
[addr] high=0xffffffc000001234 page_start=0xffffffc000001000
[addr] invalid Sv39 address rejected
```

之后原两应用继续抢占运行，各退出0一次，最终QEMU主动关机退出0。
默认仍使用/usr/bin/qemu-system-riscv64系统10.2.1，不切换到PATH中的开发版。

P0由协作者承担，作者新增测试框架代码为零：

- 正常/不变量：上述低VA和PA精确数值；三级索引顺序；高canonical VA的27位VPN、
  页首还原和偏移；四类型构造不接受超出各自表示范围的输入。
- 拒绝：0x0000004000000000必须返回None，不能截断为另一个有效地址，也不能panic。
- 接入后的集成确认：地址演示不改CSR/内存映射，原heap释放统计及应用调度退出仍通过。

这些位运算不依赖CSR，可以在临时64位宿主程序中验证；真实启动集成仍用QEMU。
协作者不会将完整边界穷举或故障矩阵追加成作者作业，也不要求此时实现页表。
本批只有文档时，临时骨架预检不等于作者实现通过；实际完成后再做集中审查。

2026-10-17文档预检：从第5节提取骨架，仅在临时目录补齐六处TODO，
使用固定rustc1.98.1、edition2024和-D warnings在64位宿主编译并运行，均退出0，
三条输出与上面完全一致，断言均通过。缓存 `.cache/batch11-doc-preflight-7a2u32ms/`。
此记录仅验证文档骨架的纯计算；尚未在作者内核接入或运行本批地址演示。

2026-10-18作者实现验收：独立只读复核六处算法及接线，无必修功能问题。
实际timeout 8s make run无警告构建、退出0；低/高地址拆分、页首还原、构造范围与非法输入拒绝
均通过演示断言，堆释放归零，原两应用三轮计算、双向timer抢占和各exit0一次正常。
仅执行既定必要验证，未扩展矩阵或代改作者实现；os/user源码及配置哈希前后一致。
日志与校验记录 `.cache/batch11-author-review-8tuicwgt/`。这仍是地址纯计算，未建立页表或开启Sv39。

## 9. 理解题与答案审查（2026-10-18）

以下保留作者原答，再逐题记录审查与修正版。前两题主线正确；第三题作者表示尚不清楚，
本节补充完整推导，不将补充讲解记作作者已独立答对。

### 第1题：VA/PA为何分类型，floor/to_addr是否完成翻译？

**作者原答：**

> 利用rust的特性防止VA和PA混用，floor只是负责得到PPN和VPN，也就是一个对应目的地址的导航，to_addr也没有得到接续offset得到偏移为0的物理地址，但是只是做了表示，没有查页表找到真正的物理页，并不能算翻译过去了

**审查：** 已正确区分类型表示与实际翻译。需要明确floor所得页号的种类，
以及to_addr并非统一返回物理地址；“导航”只是比喻，这些方法本身不访问任何页表。

**修正版：**

VA与PA语义不同，即使当前数值相同，也不保证它们在另一套映射中仍然相同。
使用不同的Rust包装类型，可以让编译器阻止将VirtAddr直接当作PhysAddr传入相应接口。

| 操作 | 输入与结果 | 是否查页表 |
| --- | --- | --- |
| VirtAddr::floor | 虚拟字节地址→VPN，去掉偏移和高位扩展 | 否 |
| VirtPageNum::to_addr | VPN→虚拟页首，偏移补0并恢复canonical高位 | 否 |
| PhysAddr::floor | 物理字节地址→PPN，去掉偏移 | 否 |
| PhysPageNum::to_addr | PPN→物理页首，偏移补0 | 否 |

floor是得到地址所属的页号，to_addr是恢复该页的起始字节地址，二者均未完成虚实地址翻译。
后续需要查找VPN对应的PPN，再接回原页内偏移，才能得到对应的物理字节地址；
实际访问还需要满足映射有效性和访问权限等条件。

### 第2题：为什么偏移12位、VPN27位，三级索引按什么顺序？

**作者原答：**

> 因为我们要4096字节对齐，而2^12 = 4096,VPN每一节都占据9位，共3节就是27位，1属于VPN0，两个2分别属于VPN1,VPN2

**审查：** 位数和各索引归属正确。偏移位数首先来自页大小：一页中有4096个字节位置，
需要12位加以区分。页首4096字节对齐则表现为地址低12位为0。

**修正版：**

4KiB=4096=2^12，因此页内偏移占12位；Sv39的39位地址信息扣除偏移，
剩余39-12=27位VPN。三级页表每级取9位，每级可索引512项。

VA 0x80401234得到VPN 0x80401和offset 0x234，indexes返回：

| 数组位置 | 数值 | 位段 | 后续查表用途 |
| --- | --- | --- | --- |
| indexes[0] | 2 | VPN[2]，VA bits[38:30] | 根页表索引 |
| indexes[1] | 2 | VPN[1]，VA bits[29:21] | 中间页表索引 |
| indexes[2] | 1 | VPN[0]，VA bits[20:12] | 最后一级页表索引 |

所以[2, 2, 1]是从根到叶的顺序；数组下标0指最高一组VPN[2]，不能将两种编号混为一谈。

### 第3题：canonical地址与高位VPN提取

问题：为什么0xffffffc000001234合法，而0x0000004000000000不合法？
高半区VA直接右移12位当作VPN，会把哪些不该保存的位带进去？

**作者原答：**

> 最后不知道

**审查：** 本题缺口在于完整64位表示与低39位地址信息的区别，补充如下。

**修正版与讲解：**

Sv39用低39位表示地址信息，但当前寄存器和usize都是64位。
规范要求完整地址bits[63:39]全部等于bit38，这称为canonical形式：

```text
bits[63:39]       bit38      bits[37:0]
必须全部等于bit38   │         其余地址位
                   └── 低39位的最高位
```

- bit38为0，高25位必须全0。
- bit38为1，高25位必须全1。
- 0xffffffc000001234的bit38为1、高25位全1，满足规则。
- 0x0000004000000000的bit38为1、高25位却全0，不满足规则，构造时返回None。

这只是地址格式检查；格式合法仍不保证存在映射或允许访问，也不表示本批已经开启Sv39。
VirtAddr内部保留完整64位表示，提取VPN时必须去掉额外的符号扩展位：

```rust
VirtPageNum((self.as_usize() & VA_MASK) >> PAGE_SIZE_BITS)
```

直接右移12位会让原高25位的1仍留在结果中，得到超出本批27位VPN表示范围的数值。
先用VA_MASK保留低39位，再右移12位，才得到正确VPN：

```text
完整VA： 0xffffffc000001234
低39位： 0x4000001234
VPN：    0x4000001
offset： 0x234
```

反过来，VPN左移12位先得到0x4000001000；由于bit38为1，还需将高25位补1，
最终恢复虚拟页首0xffffffc000001000。只左移、不补高位，会得到非canonical地址。

## 10. 参考与范围

- [教材：地址空间](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter4/2address-space.html)
- [教材：Sv39与地址表示](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter4/3sv39-implementation-1.html)
- [RISC-V规范：Supervisor，Sv39章节](https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html)

教材书源码固定fe16ac12feea76333a6d189bbabf09594ff94cef，
代码对照ch4 f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79的os/src/mm/address.rs。
索引统一采用规范VPN[2]最高、VPN[0]最低的命名，indexes数组按根→叶排列。
来源固定不表示整个官方ch4已构建验收，本批也不引入其页表数组/裸指针访问代码。
