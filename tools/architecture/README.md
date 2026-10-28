# 本地架构分析

三个独立工具入口已接入，不改变内核编译/链接，也不向服务器上传源码或调用模型 API。安装步骤联网下载公开工具；分析在 WSL 本地运行。

| 工具 | 用途 | 输出（默认 kernel 范围） |
| --- | --- | --- |
| libclang | 可编程 AST、函数定义、静态调用和头文件依赖 | `.cache/architecture/kernel/graph.json`、`calls.mmd` |
| clang-uml | 自动生成头文件图和粗粒度目录依赖图 | 同目录 `includes.mmd`、`modules.mmd` |
| Joern | 后续查询 AST/CFG/数据流的代码属性图（CPG） | 同目录 `cpg.bin` |

已在 Ubuntu 26.04 x86-64 / WSL2 验证。工具、第三方许可证、安装包和生成结果均在已经被 Git 忽略的 `.cache/architecture/`。没有把工具源码或二进制加入仓库，没有安装后台服务，也没有使用 sudo。

## 使用

当前工作区已完成安装，可以直接执行：

```bash
make architecture                       # 默认内核：三个后端顺序执行
make architecture-libclang              # 只刷新 JSON/调用图
make architecture-uml                   # 只刷新头文件/目录图
make architecture-joern                 # 只刷新 CPG
make architecture ARCHITECTURE_SCOPE=user
make architecture ARCHITECTURE_SCOPE=host
make architecture-test                  # 真实 libclang 的小型回归测试
```

`kernel` 包括实际构建中 kernel/ 各子目录、drivers/ 和 lib/ 的 C 源码；`user` 只包括用户 C 程序；`host` 从独立宿主 CMake 工程的数据库选择实际编译单元，包括该工程链接的内核算法源码。不要把 RV64 和宿主数据库混为一个分析配置。

每次运行重新生成范围专用的 `compile_commands.json`：RV64 范围读取 `make -Bn all` 的编译命令，不执行内核构建；host 配置 CMake，但不运行测试/编译。移除输出/依赖文件生成参数，保留宏、包含路径和语言选项；RV64 显式指定 target，补上 Clang 内置头文件路径。不覆盖根目录给 clangd 用的编译数据库。

`.mmd` 是 Mermaid 文本，可在支持 Mermaid 的 Markdown 预览中查看（例如把内容放入 Mermaid 代码块）。生成图是当前源码快照，修改源码后重新运行命令。

## 直接查看与导出

```bash
make architecture-export                 # 把已有分析结果导出为 HTML + SVG + DOT
explorer.exe "$(wslpath -w .cache/architecture/kernel/export/index.html)"
```

查看页在 `.cache/architecture/kernel/export/index.html`，支持图表选择、缩放和 SVG/DOT 下载。默认先看头文件依赖，再选择 `kernel_main` 等局部调用图；完整调用图较大，可以滚动查看。Joern CFG 按函数列出，箭头表示控制流。整个查看目录只读本地图片，没有 CDN 或服务器请求；HTML 与图片要放在一起，分享时复制整个 `export/` 目录，单独的 SVG 则可以独立分享。

导出也可选择范围：`make architecture-export ARCHITECTURE_SCOPE=user` 或 `host`，对应查看页在该范围的 `export/index.html`。导出命令不重新解析源码；修改源码后运行 `make architecture && make architecture-export`，刷新分析再导出。Joern 每次从当前 CPG 导出到新临时目录，避免旧 DOT 与当前 CPG 混用；最终同名导出文件会覆盖，目录内旧的未引用图片不会自动删除。

导出需要 Graphviz。当前已安装在缓存；若旧安装缺少它，运行 `python3 tools/architecture/install.py --backend export`。外部 PATH 中的 `dot` 也可使用。Graphviz 布局仅改变表现形式，不补充缺失的静态关系；其上游许可证应独立检查，以缓存 `usr/share/doc/graphviz/copyright` 为准。

## 重新安装

```bash
make architecture-install
# 或分别安装：
python3 tools/architecture/install.py --backend libclang
python3 tools/architecture/install.py --backend uml
python3 tools/architecture/install.py --backend joern
```

缓存安装器仅支持当前 Ubuntu 26.04 x86-64，需要已有 python3/venv、curl、apt-get、dpkg-deb。libclang 入口还需要系统 Clang/`libclang-21.so.21`，当前系统已具备。安装器下载并解包 apt 依赖到缓存，不执行系统安装。APT 依赖版本随已配置的软件源选择，GitHub 工具版本固定如下：

- clang-uml 0.6.3：使用官方 questing 包及 LLVM 20 动态库；固定 SHA-256 校验。不同 Ubuntu 包名并不表示所有包使用同一 LLVM 版本。
- Python clang 21.1.7 绑定配合系统 libclang 21；不使用关闭兼容性检查来混搭版本。
- Joern v4.0.652：官方 Linux x86-64 ZIP，固定 SHA-512 校验；解压后恢复执行位。
- OpenJDK 25：缓存解包并把 Java 的绝对配置链接重定位到缓存；当前实际运行版本 25.0.4.1。

Joern ZIP 约 1.8 GB，解压及保留下载包需要数 GB。已存在的完整下载会复用；校验失败会停止，不自动删除文件。许可证原文保存在 `.cache/architecture/licenses/`，APT 包保留自己的 `usr/share/doc/*/copyright`。普通分析命令不负责下载依赖。

其他系统可自行安装 clang-uml/joern-parse 到 PATH；分析入口优先使用 PATH 中的工具。libclang 入口当前按本机 LLVM 21 路径配置，需要匹配的 Python 绑定；跨系统移植时应一起调整，不能只替换动态库。

## Joern 查询和导出

Joern 收到只包含所选编译单元和项目头文件的临时源码快照，以及重定位的范围专用编译数据库。头文件包括编译单元同目录的头，以及递归收集的 include/ 分组头。原始源码内容保存在 CPG；临时快照在完成后回收。`--log-problems` 启用解析问题日志，但 CPG 成功生成仍不能证明每个语义关系完整。

启动交互式控制台（默认内核）：

```bash
export JAVA_HOME="$PWD/.cache/architecture/java/usr/lib/jvm/java-25-openjdk-amd64"
.cache/architecture/joern/joern-cli/joern
```

在控制台中：

```scala
importCpg(".cache/architecture/kernel/cpg.bin")
cpg.method.name.l
cpg.method.nameExact("kernel_main").call.name.l
cpg.method.nameExact("user_memory_release").cfg.dot.l
```

导出 DOT（输出目录必须尚不存在）：

```bash
JAVA_HOME="$PWD/.cache/architecture/java/usr/lib/jvm/java-25-openjdk-amd64" \
  .cache/architecture/joern/joern-cli/joern-export \
  .cache/architecture/kernel/cpg.bin --repr cfg --format dot \
  --out .cache/architecture/kernel/joern-cfg
```

参见 [Joern 导出文档](https://docs.joern.io/export/)。Joern 使用自己的 C/C++ 前端，并不会执行 RV64 指令；消费编译数据库不等于完全复现 GCC 的语义。Java/Scala 当前有弃用提示，Joern 对部分 CFG 使用 fallback，也应在需要精确的数据流/控制流结论时人工复核。

## 范围与限制

- 当前没有生成汇编 AST。entry/trap/user syscall 汇编、CSR、链接脚本、context switch 和硬件触发关系要另外整理，不能把静态图当成运行时架构。
- libclang 输出函数模板、构造/析构等定义，但 lambda、动态派发、函数指针、宏和模板实例化的调用归属仍可能不完整。外部调用保留在 JSON 中；Mermaid 调用图只连当前收集到定义的节点。
- 图按源代码符号和 include 关系生成；“PMM → VM → 用户内存”等设计分层还需要结合代码和运行验证来解释。clang-uml 的目录图只表达其静态模型，不是完整的模块调用图。
- 解析错误会保留诊断和部分 JSON，并返回失败；有失败的结果不能当成完整架构。三个后端结果不要互相当成正确性证明。
- 当前接入没有把分析结果接到 CI，也没有要求每次 kernel/test 构建自动运行这些工具。

## 许可证

仅在本地运行这些外部工具，不要求把 TinyOS 改成它们的许可证，也不要求在每个内核源码文件插入它们的 license。当前提交内容只是我们自己的调用脚本、配置生成和说明；工具本体留在忽略缓存中。

| 组件 | 上游许可证 |
| --- | --- |
| clang-uml | [Apache-2.0 原文](https://github.com/bkryza/clang-uml/blob/0.6.3/LICENSE.md) |
| Joern 本体 | [Apache-2.0 原文](https://github.com/joernio/joern/blob/v4.0.652/LICENSE) |
| LLVM/libclang 与 Python clang 绑定 | [Apache-2.0 WITH LLVM-exception 等上游说明](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.7/LICENSE.TXT) |
| OpenJDK 运行时 | GPL-2.0，相关文件附 Classpath exception；以缓存包 copyright/上游逐文件说明为准 |

如果以后把第三方工具二进制/源码一起打包分发，或复制其代码进入仓库，就要履行对应分发义务：保留 license、版权和适用的 NOTICE，按条款标明修改，并检查传递依赖。Joern 的 Java/Scala 依赖以及 OpenJDK 不自动全部适用 Joern 本体的 Apache 许可证；这份工具清单不是整个分发包的完整许可证清单。仅分享本仓库调用脚本和安装说明，与重新分发那些二进制是不同的范围。

2026-10-09 验证：三个后端的 kernel/user/host 范围均成功；内核 CPG 的 CFG DOT 导出成功；真实 libclang 的调用/include、首编译单元错误、C++ 模板三个回归测试通过。没有执行 QEMU，也没有用静态图验证运行时行为。
