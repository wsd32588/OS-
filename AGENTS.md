# Agent Entry Point

开始处理本仓库前，必须完整阅读并遵守 `AGENT.MD`，同时阅读 `MEMORY.MD` 了解当前实现、验证结果和待办事项。

## Codex Desktop / WSL 命令执行故障

- 如果工具在命令执行前报 `Failed to create unified exec process: No such file or directory (os error 2)`，但手动 WSL 终端正常，先检查活动后端的 `arg0` 辅助目录；不要直接认定 Bash、项目目录或 WSL 安装损坏。
- 2026-10-01 已验证：桌面 WSL 后端仍持有 `.lock`，但对应 `codex-arg0*` 目录已消失。重建该确切目录和辅助链接后，同一聊天的 `pwd`、`uname -a` 立即成功。独立 CLI 的 `codex sandbox -- /bin/sh -c 'pwd'` 在修复前也能成功。
- Windows 侧 Codex 清理共享临时目录是相关报告中的解释；本机未捕获实际删除进程。`bwrap` 源文件缺失日志不能单独证明执行失败的原因，也没有证据指向杀毒软件。

工具已失效时，请用户在手动 WSL 终端执行以下只读诊断：

已提供自动诊断和临时恢复脚本：`python3 /home/asus/tinyos/tools/repair_codex_wsl.py`；`--check` 只读检查，`--watch` 每 10 秒重新发现并恢复缺失链接，Ctrl+C 停止。无需手动指定 PID/目录/版本，无后台服务安装。优先让用户在手动 WSL 终端运行；工具沙箱可能看不到活动后端。范围与限制见 `tools/README.md`。若脚本报告冲突，不覆盖，应继续检查。

```bash
for pid in $(pgrep -f 'codex.*app-server'); do
    printf '\nPID=%s\n' "$pid"
    readlink /proc/"$pid"/exe
    readlink /proc/"$pid"/fd/* 2>/dev/null |
        rg 'codex-arg0.*[.]lock'
done
```

根据输出逐一检查目录是否存在、`codex-linux-sandbox` 是否能由 `readlink -e` 解析。无 `(deleted)` 标记不代表目录仍存在。区分 WSL 本地后端与桌面启动的 `/mnt/c/...` 后端；不要猜测当前 PID、随机目录名或版本哈希。

确认活动后端的辅助目录丢失后，可按以下步骤临时恢复：

1. 使用当前诊断得到的确切路径，在 Windows PowerShell 中执行 `New-Item -ItemType Directory -Force -Path '<对应 Windows 路径>'`。此前通过 Windows 创建目录成功；DrvFS 上直接重建可能遇到异常。
2. 在 WSL 中，将 `helper_dir` 设置为该目录，将 `codex_binary` 设置为对应活动进程的 `/proc/<PID>/exe` 所指向的可执行文件；先确认目标存在且可执行。
3. 恢复链接，不覆盖已有文件或链接：

```bash
for helper_name in codex-linux-sandbox codex-execve-wrapper apply_patch applypatch; do
    ln -s "$codex_binary" "$helper_dir/$helper_name"
done
ls -la "$helper_dir"
```

4. 立即通过当前聊天工具执行 `pwd`、`uname -a`，确认恢复；若目录存在或恢复无效，继续诊断其他原因。

这是临时恢复，不会阻止再次清理。不要硬编码本次随机目录、批量重建所有目录、删除 `.codex`、放宽权限或关闭安全防护。不要仅为排查而启动 Windows 原生 Codex 进程，因为相关报告观察到它可能再次触发清理。`node_repl` 的 `sandboxCwd is not a local file URI` 是另一项未验证恢复的错误，不能因 shell 恢复就宣称所有工具正常。

参考：[同版本报告 #49731](https://github.com/openai/codex/issues/49731)、[复发与临时恢复 #25317](https://github.com/openai/codex/issues/25317)、[恢复验证记录 #17570](https://github.com/openai/codex/pull/17570)。
