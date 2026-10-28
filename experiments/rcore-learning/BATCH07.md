# BATCH07：保存内核续点，用 __switch 切换执行流

位置：第三章，多应用共存之后、协作调度之前。
2026-10-15：作者已完成本批核心并移除 guard，功能验收通过。
当前两轮往返、首次用户入口及退出正常；具体运行记录和固件尾片观察见 PROGRESS.md。
以下保留整批原实现要求，供复习两个构造函数和五条汇编指令的职责。

## 1. 这一批要观察什么

先让启动代码和一段内核演示代码来回切换两轮，再通过同一切换机制首次进入用户应用：

```text
boot → demo 第一次执行 → boot 第一次续接
     → demo 从上次暂停处续接 → boot 第二次续接
     → 首次用户入口 → 用户 write/exit
```

boot 和 demo 都在 S-mode。只有最后的 `sret` 进入 U-mode。
这一步建立调度器要调用的切换机制；应用仍只运行 START_APP_ID 选中的一份。
本批没有选择下一个应用、yield、TaskStatus、时间片或定时中断。
下一批才把任务状态、当前任务编号和选择规则接在这个机制上。

## 2. 为什么另有 TaskContext

| 对象 | 保存什么 | 谁使用 | RV64 大小 |
| --- | --- | --- | --- |
| TrapContext | 用户整数寄存器槽 x[32]、sstatus、sepc | __alltraps / __restore | 272 字节 |
| TaskContext | 内核调用的 ra、sp、s0..s11 | __switch | 112 字节 |

Trap 是在用户执行到某个位置时发生的，用户寄存器需要由 trap 入口保存。
而 `__switch` 是 Rust 主动发起的一次普通 C ABI 函数调用：编译器知道调用会改写
caller-saved 寄存器，需要继续使用的值会自行安排保存。
我们只需额外存下这条内核执行路径的续接地址、栈指针和 callee-saved 的 s 寄存器。
以后从 trap handler 调用切换时，完整用户现场已经保存在自己的 TrapContext 中。

本项目尚不使用浮点/向量任务现场；gp/tp 也没有各任务不同的含义。
这不是所有架构扩展、TLS、嵌套 trap 的完整上下文方案。

`TaskContext.s[0]` 对应 s0/x8，s[1] 对应 s1/x9，s[2] 对应 s2/x18；
它不是 TrapContext.x 的编号体系。TaskContext.sp 才单独记录 x2/sp。

## 3. 文件关系与已经接好的路径

| 文件 | 职责 | 作者完成点 |
| --- | --- | --- |
| os/src/task/context.rs | TaskContext 布局、两个构造函数 | 两个函数的 TODO |
| os/src/task/switch.S | 保存旧续点、恢复新续点；首次启动适配入口 | A/B/C 共五条指令 |
| os/src/task/mod.rs | FFI、两轮往返演示、首次进入包装 | 核心完成后删除 guard |
| os/src/main.rs | 装载、准备现场、演示、进入选定应用 | 已接好 |
| os/src/trap/trap.S | 原有用户现场恢复和 sret | 本批不需改动 |

旧 `trap::enter_user` 包装已移除，首次启动由 `task::enter_first` 接管。
普通用户 write 返回仍由 trap handler 返回现场地址，经原 __restore 恢复。
用户 exit 仍关机，不会切到另一个应用。

完整首次路径：

```text
rust_main
  → clear_bss / trap::init / load_apps / prepare_app(0 和 1)
  → task::run_switch_demo(START_APP_ID)
  → task::enter_first(选中的 TrapContext 指针)
    → TaskContext::goto_restore
    → __switch(boot_context, first_context)
      → ret：跳到 __task_bootstrap，sp 已指向 TrapContext
        → mv a0, sp
        → j __restore
          → 恢复用户寄存器和 CSR
          → sret：首次进入 U-mode
```

## 4. 作者实现 A：两个构造函数

TaskContext 已使用 `#[repr(C)]`，并断言大小和字段偏移：

| 字段 | 偏移（字节） |
| --- | --- |
| ra | 0 |
| sp | 8 |
| s[0] / s0 | 16 |
| s[i] | 16 + i × 8 |
| s[11] / s11 | 104 |

`zero_init()` 返回字段全零的 Self。用途是准备一块供 __switch 写入的记录。
ra=0、sp=0 的记录尚不能作为 next 运行，必须先实际保存，或者构造合法首次入口。

`goto_restore(trap_context_address)` 返回：

- ra：`__task_bootstrap as *const () as usize`。
- sp：传入的 trap_context_address。
- s：12 个零。

函数名转指针再转整数只是在取得入口地址，并没有执行该函数。
返回结构体可直接用 `Self { ra: ..., sp: ..., s: ... }`。
现有 `let _ = trap_context_address;` 是骨架占位，完成后可以删掉。

这里为何不是 ra=__restore？因为我们从第二章保留的 __restore 第一条是
`mv sp, a0`，它要求 a0 传入现场地址。而 __switch 的 a0 是旧 TaskContext 的地址。
已经提供的 __task_bootstrap 用 `mv a0, sp; j __restore` 补齐这个接口。
固定官方第三章版本的 __restore 不含那条 `mv sp, a0`，所以不能照搬它的入口构造而忽略区别。

## 5. 作者实现 B：__switch 的五条指令

函数声明是普通可返回的 C ABI 调用：

```rust
fn __switch(current: *mut TaskContext, next: *const TaskContext);
```

进入时 a0 指向 current，a1 指向 next。两块记录有效、对齐且不重叠。
记录位于 Rust 局部变量或静态对象中，不需要在这里 `addi sp, sp, -112`。

按 switch.S 中 A/B/C 标记填写：

1. A：用两条 `sd` 将当前 ra、sp 写入 a0 指向记录的偏移 0、8。
2. B：用一条 `ld` 从 a1 指向记录的偏移 0 恢复 ra。
3. C：用一条 `ld` 从 a1 指向记录的偏移 8 恢复 sp，最后 `ret`。

语法仍是 `sd 寄存器, 偏移(基址寄存器)`、`ld 寄存器, 偏移(基址寄存器)`。
s0..s11 的重复保存/恢复已写好，沿现有顺序即可。
旧 ra/sp 必须先保存，才能覆盖为新值；恢复 sp 放在最后，便于清楚划分两条执行路径。

`ret` 使用 ra，等价于跳到 ra 指定的地址；它不改变特权级，也不读取 sepc。
`sret` 则根据 sepc/sstatus 返回被中断的执行状态，二者职责不同。
__switch 本身只做 sd/ld/ret，不读写 CSR，也不触发 ecall。

## 6. 两轮演示为何能“返回到另一处”

第一次 boot 调用 __switch 时，call 已让 ra 指向 boot 的调用后续。
汇编把该 ra、当前 boot 栈 sp 和 s 寄存器保存到 BOOT_CONTEXT。
DEMO_CONTEXT 预先填了 demo_entry 的地址和另一片栈，所以恢复它并 ret 后开始执行 demo_entry。

demo 调用 __switch 时，自己的调用后续和栈被写入 DEMO_CONTEXT，再恢复 BOOT_CONTEXT。
于是 boot 的第一次 __switch 看起来终于“返回”了，打印 resumed 1。
boot 第二次切入时，DEMO_CONTEXT 的 ra 已不是最初 demo_entry，
而是 demo 上次调用 __switch 之后的位置，所以打印 resumed，而不是重新打印 first entry。

两条路径各自在自己的栈上保留调用链。TaskContext 只存恢复这条调用链所需的寄存器；
局部变量和更深层的栈帧仍留在原栈里，没有复制到 TaskContext 中。

演示是启动阶段的教学接线，使用尚未运行应用的内核栈：

```text
高地址 K = kernel_stack_top
  [ K-272, K )         已准备的 TrapContext，必须保留
  K-272               demo 的初始 sp，16 字节对齐
  [ K-8192, K-272 )    demo 可用栈空间，7920 字节，向低地址增长
低地址
```

demo_entry 是零参数 `extern "C" fn() -> !`，首次 a0 不是 app_id，不应按参数读取。
demo 用 __switch 切回 boot，不能普通 return：首次入口的 ra 并非正常调用者返回地址。
第二次回到 boot 后，demo 永不再恢复，随后用户 trap 可以复用其旧栈空间。
演示不持有需要 Drop 的资源，也不跨切换保留对全局现场/栈的 Rust 引用。
原有单 hart、S 中断关闭、无 S-mode 嵌套 trap 的前提继续成立。

## 7. 完成与观察

填完上述内容后，删除 task/mod.rs 中的 `compile_error!`，保留布局断言。

```sh
cd /home/asus/tinyos/experiments/rcore-learning
make build
timeout 8s make run
echo $?
```

应用和栈布局日志后，必须按以下顺序出现，默认选择 app0：

```text
[switch] boot: start demo
[switch] demo: first entry
[switch] boot: resumed 1
[switch] demo: resumed
[switch] boot: resumed 2
[kernel] enter app0 through __switch
[user] Hello from U-mode!
[kernel] application exited: code=0
```

每条出现一次，无 panic，QEMU 主动关机退出 0。
完成点是内核执行流能首次进入及续接、用户启动复用 __switch；不要求两个用户应用轮流输出。

## 8. 必要验证与预检记录

P0 由协作者承担，只固定两次运行，不要求作者再写测试框架：

- 正常：两轮切换日志严格有序；两份顶部 272 字节现场所有字段保持不变；
  保存的 boot/demo 栈指针在各自范围内，demo sp 满足 16 字节对齐；再启动 app0，退出 0。
- 拒绝：演示输入 APP_COUNT（非法 id=2）返回 Err，BOOT_CONTEXT、DEMO_CONTEXT、
  两份 TrapContext 均不变；随后合法演示并通过 __switch 启动 app1，退出 0。
  同时核对 112 字节布局与 s0..s11 的保存/恢复偏移。

学习点分别是调用续接/栈归属，以及先验证再修改状态。作者新增测试代码为零。
两次可合并完成正常与拒绝路径观察，不扩展寄存器或故障矩阵。

2026-10-15 协作者仅在 `.cache/batch07-preflight-8_burpyd/` 补全核心并加入临时观察，
两个场景均无警告构建、按序输出、QEMU 退出 0；作者 os/user 源文件哈希保持不变。
这是骨架预检，不代表作者当前 TODO 已完成。改动前第六批快照在
`.cache/batch06-before-switch-9fb6yvpa/`。

## 9. 完成后集中讨论的三个问题

1. 为什么 TrapContext 保存 x[32]，而 TaskContext 只保存 ra、sp、s0..s11？
   内核局部变量和用户的 a0 各由哪里保存？
2. boot 第二次切到 demo 时，为什么从上次 __switch 后继续，而不是再次进入 demo_entry 开头？
   哪两个字段决定了续接的位置和调用链？
3. 首次用户启动为什么需要 __task_bootstrap？沿 ret → __restore → sret 追踪，
   分别说明此时 ra、a0、sp、sepc 的用途，以及哪一步改变特权级。

参考：[官方任务切换章节](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter3/2task-switching.html)。
源码对照固定在 ch3 的 `9ca4aedb66f626dcc72f835a5946742a82b61c7c`；
仅核对相关实现，不宣称官方完整第三章已通过本地验证。
