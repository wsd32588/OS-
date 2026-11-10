# 开发环境迁移与检查点恢复

2026-11-10迁移前保存点：`snapshot/pre-environment-20261110-201406`。
此标签保存当时完整Git工作内容，包含未完成的B15；不是新的功能验收标签。

## 当前实现边界

- B14的SPAWN/FORK/EXEC正常链已验证，用户ID2/4/5/3依次退出0/23/37/0。
- B15已增加Process父字段及访问/reset操作；关系登记、限父收集和WAITPID尚未完成。
- sched_configure_user_reaper目前是尚未调用的空函数，构建有unused参数与缺返回值两条警告。
  恢复后继续完成B15，不将此函数接到启动路径后直接运行。
- 本次检查点的make链接、原82个具体宿主场景、16秒正常QEMU均通过；QEMU由外部计时停止。

## 已验证工具版本

| 工具 | 版本 |
| --- | --- |
| Ubuntu | 26.04.1 LTS，WSL2 |
| RISC-V GCC | 15.2.0 |
| GNU binutils | 2.46 |
| Clang | 21.1.8 |
| QEMU | 10.2.1 |
| CMake | 4.2.3 |
| Python | 3.14.4 |
| GDB | 17.1 |
| Bear | 3.1.6 |

版本用于重现参考，不代表必须一字不差才能构建；其他环境需实际验证。
编译命令以Makefile为准，仍为-O0与freestanding C++20。
可选架构分析安装器另有Ubuntu26.04限制，见tools/architecture/README.md。

## 从Git恢复源码

在新目录克隆仓库后，可用标签创建实验分支：

```sh
git switch -c environment-trial snapshot/pre-environment-20261110-201406
make -B -j4
make test CTEST='ctest --verbose'
timeout 25s make qemu
```

使用新副本重建，不搬用旧CMake缓存、对象文件或clangd编译数据库。
源码恢复后先读AGENT.MD、MEMORY.MD及BATCH15任务，保留既有未完成状态。

## 本地快照

快照目录：`D:\Backups\TinyOS\20261110-201406`。
Git bundle保存Git历史/引用，源码归档保存提交树；环境清单与验证日志单独保存。
这些与完整WSL发行版导出是不同层级，源码归档不包含忽略的工具缓存和用户凭据。
Ubuntu.tar导出曾中断并留下零字节文件，该文件不是可用备份。
目录内另备Export-Ubuntu.ps1供结束WSL内开发任务后，在Windows PowerShell执行离线导出。
只有脚本成功并生成校验值后，才可将Ubuntu-offline.tar视为已完成的环境备份。
WSL导出不包含Windows侧的.wslconfig，需同时保留目录中的独立配置副本（若该文件存在）。
