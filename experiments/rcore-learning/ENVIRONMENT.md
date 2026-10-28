# 环境与复现

核对日期：2026-10-11。宿主为 Windows WSL2 Ubuntu，x86_64。

| 工具 | 当前事实 / 使用方式 |
| --- | --- |
| Rust / Cargo | 默认 stable 实际为 1.98.1；另有 1.90.0，仅宿主目标 |
| RISC-V 目标 | stable 已有 riscv64gc-unknown-none-elf，包含裸机 core |
| QEMU | 系统 `/usr/bin/qemu-system-riscv64` 10.2.1；首章参考与作者 BATCH01..09 相关路径已实测 |
| GNU RISC-V binutils | 2.46；使用 objcopy/readelf/objdump，无需 cargo-binutils |
| GDB | gdb-multiarch 17.1 可用，本批不要求先配置调试器 |
| SBI | 系统 OpenSBI 1.8.1 已有；本工作区使用快照随附 RustSBI |

没有安装/升级全局 Rust、目标或 QEMU，没有修改 shell / Cargo 全局配置。
不使用 rustup default。`scripts/cargo.sh` 定位现有 stable 的编译器，核对完整字符串
`rustc 1.98.1 (48a229cea 2026-09-01)`；如果以后全局 stable 升级，脚本停止，
需显式补装/选择固定 1.98.1，或重新验证并更新记录。这不是把可变通道名当永久锁。
暂不放可能触发自动下载的 rust-toolchain 文件。

Cargo 下载放 `.cache/cargo/`，作者构建使用 `--offline --locked`。
官方 Makefile 的 env 会尝试安装组件和 cargo-binutils，所以没有运行它。
直接用 Cargo 编译官方副本，GNU objcopy 转二进制。首章不需要 rust-src 或 llvm-tools-preview；
后续按实际需要准备，不先安装全部工具。

## QEMU 选择与关机兼容性（2026-10-17）

Makefile 默认使用绝对路径 `/usr/bin/qemu-system-riscv64`，仍允许用 `QEMU=...` 显式覆盖。
原因是作者的交互式 Bash 把 `/home/asus/src/Sys/qemu/build` 放在 PATH 前面，
裸命令实际选择 QEMU 11.1.50（v11.1.0-1664-gc1c18d1e64）开发版，
与此前工具环境使用的系统10.2.1不同。此次没有修改 shell 配置或卸载开发版。

同一份 BATCH09 内核、相同固件和启动参数：系统10.2.1正常关机退出0；
开发版在 all applications exited 后不关机，8秒后由timeout终止、返回124。
开发版的关机设备树节点为 `/soc/syscon@100000`，10.2.1使用 `/soc/test@100000`。
仅在缓存中把开发版DTB的该节点改回 `test@100000`，开发版也关机退出0，
表明当前旧RustSBI的关机路径与该节点命名有兼容问题；不是作者timer调度代码错误。
项目采用已验证的系统版，不引入临时DTB改名或替换固件作为正常运行步骤。

已模拟开发版优先的PATH复验默认 `timeout 8s make run`，实际调用系统版且退出0。
现在可继续使用原命令；单独运行裸 `qemu-system-riscv64 --version` 仍可能显示开发版。
对照版本/镜像哈希、节点改名前后DTB及日志在 `.cache/batch09-author-review-yjy0gr0n/`。

## 固定参考的恢复与复现

准确来源/校验见 `sources.lock.json`。已下载 ch1 archive（42307 字节），
随附固件（32328 字节）；参考 GPL-3.0 原文保留在 `.cache/reference/LICENSE`。
自己的实现不复制整份参考；后续直接移用代码时保留其出处与许可证。

普通工具沙箱下载因 socket / proxy 权限失败；经执行权限复核后，
GitHub 和 crates.io 下载成功。现在首批依赖已齐，可离线编译，不需作者处理网络。

如果缓存丢失，可在本目录恢复缺失项，不能覆盖作者 `os/`：

```sh
set -e
cd /home/asus/tinyos/experiments/rcore-learning
mkdir -p .cache/downloads .cache/reference
curl --fail --location \
  https://codeload.github.com/rcore-os/rCore-Tutorial-v3/tar.gz/85fe952a04c4b671fef1beb2f470591ae7ea5123 \
  --output .cache/downloads/ch1.tar.gz
echo 'db481b41c900e2eafb67e6db3edc05001b029dcda5561dc7053f73bf7bbd7e0d  .cache/downloads/ch1.tar.gz' | sha256sum -c
tar -xzf .cache/downloads/ch1.tar.gz -C .cache/reference --strip-components=1
echo '31f5d013f81421ed9fd33208dfa1c3796f9d72bd11c0f939566581ac2f3e97d3  .cache/reference/bootloader/rustsbi-qemu.bin' | sha256sum -c
cd os
bash ../scripts/cargo.sh fetch --locked
```

校验不匹配就停止。网络受限由协作者处理，不换不明镜像。
复现完整官方首章验证，仅在缓存副本补链接脚本与保存的 lock：

```sh
cd /home/asus/tinyos/experiments/rcore-learning
cp versions/ch1-reference.Cargo.lock .cache/reference/os/Cargo.lock
cd .cache/reference/os
cp src/linker-qemu.ld src/linker.ld
bash ../../../scripts/cargo.sh build --release --locked
riscv64-linux-gnu-objcopy -O binary target/riscv64gc-unknown-none-elf/release/os \
  target/riscv64gc-unknown-none-elf/release/os.bin
timeout 8s /usr/bin/qemu-system-riscv64 -machine virt -m 128M -smp 1 -nographic \
  -bios ../bootloader/rustsbi-qemu.bin \
  -device loader,file=target/riscv64gc-unknown-none-elf/release/os.bin,addr=0x80200000
```

本次实测：ELF64 小端 RISC-V，入口 0x80200000；banner 显示 RustSBI 库 0.3.1、
QEMU 支持包 0.2.0-alpha.2；输出 Hello 与布局日志，正常关机退出 0，不是 timeout 124。
参考依赖锁：log 0.4.34、sbi-rt 0.0.2、sbi-spec 0.0.4、static_assertions 1.1.0。
作者工作区使用sbi-rt、sbi-spec和static_assertions；格式化输出使用core::fmt。

2026-10-17 BATCH10新增精确依赖buddy_system_allocator0.6.0及其传递依赖spin0.7.2，
实际Cargo.lock与MIT许可证已保留，下载只进入本项目.cache/cargo。
缓存丢失时，在os目录执行上述 `bash ../scripts/cargo.sh fetch --locked` 恢复当前锁定依赖，
之后继续离线构建；不启用旧版const_fn feature，不修改全局Rust或Cargo配置。

2026-10-18 BATCH12将既有spin0.7.2列为直接依赖，用于页分配器Mutex。
离线更新Cargo.lock仅增加根包到spin的依赖关系，既有包版本/校验未变，无新增下载或工具升级。

## VS Code / rust-analyzer

2026-10-12 已修正根 `.vscode/settings.json`，保留此前C/CMake配置。
Rust包不在根目录，旧RA日志明确显示找不到项目；现在显式关联本子项目Cargo.toml，
使用RISC-V目标、局部依赖缓存、离线保存检查，不检查裸机test harness。
WSL扩展自带RA可用，不需要安装rustup的rust-analyzer组件。

为分析core，另下载与当前编译器匹配的rust-src，解压到本子目录缓存：

- URL：https://static.rust-lang.org/dist/2026-09-03/rust-src-1.98.1.tar.xz
- SHA256：5c846ebcebcc7e2e0777a4cdaa12051691593f16a7e94edbae5e6241cc62d98c
- sysrootSrc：`.cache/ide-rust-src/rust-src-1.98.1/rust-src/lib/rustlib/src/rust/library`

URL/哈希取自本机已安装工具链的multirust-channel-manifest.toml；没有升级全局Rust。
缓存丢失需恢复上述固定包，不能直接用不同版本的标准库源代替。

在VS Code命令面板执行 `rust-analyzer: Restart server`，再保存Rust文件。
部分编译器诊断由保存时cargo check产生，未保存不保证全部即时出现。
当前代码的cargo check/RA扫描正常；临时删除write_volatile周围unsafe块，
两种检查均报告E0133，探针没有写入作者源码。

仅取得外部符号地址的addr_of!在当前Rust允许不包unsafe；真正裸指针写入仍需要unsafe。
修改前的完整工作区配置备份在`.cache/vscode-before-wjddu5b0/settings.json`。
