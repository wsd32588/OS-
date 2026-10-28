# Codex WSL 辅助目录恢复

在手动 WSL 终端运行，无需第三方 Python 依赖或 sudo：

```bash
python3 /home/asus/tinyos/tools/repair_codex_wsl.py
```

脚本从同用户活动 `codex app-server` 进程的 `/proc` 可执行文件和锁描述符发现路径，只处理这些进程引用的目录。既有文件和冲突链接不会覆盖；健康目录不写入。找不到进程、只读检查发现缺失或恢复失败时退出码为 1，成功为 0。

只检查：

```bash
python3 /home/asus/tinyos/tools/repair_codex_wsl.py --check
```

保持该终端运行，每 10 秒重新发现进程并修复缺失链接（Ctrl+C 停止）：

```bash
python3 /home/asus/tinyos/tools/repair_codex_wsl.py --watch
```

未安装后台服务或开机任务。若其他聊天仍在运行，它们所属的同用户 Codex 后端也可能被发现；只恢复确认缺失的辅助链接，不扫描并修复所有缓存目录。脚本不启动 Windows 原生 Codex，不关闭安全防护，不修复 node_repl 的 URI 错误，也不能阻止 Codex 自身再次删除目录。

优先在 WSL 创建目录。若 DrvFS 报目录已存在但实际不可访问，再通过 `wslpath` 和 Windows PowerShell 创建同一个确切目录；需要 WSL 的 Windows 互操作功能。进程信息受限的工具沙箱可能报告未发现后端，应改在手动终端运行。修复后仍须让聊天执行 `pwd` 确认恢复。

测试：`python3 -B tools/test_repair_codex_wsl.py`。测试覆盖恢复、重复运行、只读检查、冲突文件/断链及目录符号链接保护；Windows 目录创建回退和真实复发监测尚未自动验证。

# 本地架构分析

clang-uml、libclang 和 Joern 的安装与运行入口见 [architecture/README.md](architecture/README.md)。工作区已安装到忽略缓存，使用 `make architecture` 刷新内核的依赖图、调用关系和 CPG。

# 同步源码到 Windows

`make sync` 把当前项目源码同步到 `/mnt/d/PROJECTS/C/tinyos`。同步读取根 `.gitignore` 的排除规则，并另外排除 `.git`，因此分析工具/下载包/图表缓存、CMake/用户程序/试验构建目录和 Python 缓存不会复制。根规则需要同时兼容 rsync，不使用 `!` 反向包含语法。

```bash
make sync-dry-run                         # 预览复制列表与大小，不写入目标
make sync
make sync SYNC_DEST=/mnt/d/another/tinyos  # 可覆盖目标目录
```

同步保留源码、文档、脚本和项目配置文件，包括尚未提交的内容。不自动删除目标已有文件；以前已复制过去的 `.cache` 和构建产物不会被这个新规则清除。若需要分享架构图，可单独复制 `.cache/architecture/kernel/export/`。
