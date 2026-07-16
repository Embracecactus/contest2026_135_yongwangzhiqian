# 2026-07-16 RV1126B P1 收敛构建记录（build-only）

> 本记录为 **build-only** 证据归档。本次未执行 SDK rtt.bin 替换、mkimage、amp.img 打包、update.img 重新打包、烧录、或任何板端 NSH/UART/help/uname/ps 验证。

## 候选来源

- Contest 仓库：`$CONTEST`
- 分支：`submit-rv1126b-nsh-baseline`
- 构建时 HEAD 基线：`1c42588`，叠加当前未提交的 P1 diff
- 构建后端：classic Make（唯一已验证后端）
- 构建日期：2026-07-16，本地时间 02:59

## 构建命令

从 `$WORKSPACE` 执行：

```bash
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
```

exit code：**0**

## 构建产物

| 产物 | 大小 | sha256 |
| --- | ---: | --- |
| `$WORKSPACE/nuttx/nuttx` | 182916 bytes | `378c8a78f0625e03f2d5911e4bbb7cff1e4c2f375ca060588f8c89f69462f494` |
| `$WORKSPACE/nuttx/nuttx.bin` | 98820 bytes | `26f4ae0ea2fc4398054c814bd2d79304c62ba1b530ec992835af9133ce9dbf00` |
| `$WORKSPACE/nuttx/nuttx.hex` | 278009 bytes | `8d2812cc6a4c94b683eef7253b49cac5e76e75dfb271443e8d041287f9c9f66b` |

> 构建具有非确定性。以上 hash 仅用于本次 artifact identity，不能用于证明可复现性。

## RAM 占用

- 已用：104832 B
- 总计：232 KB（237568 B）
- 占比：44.13%

## 构建警告

唯一 warning：

```text
riscv-none-elf-ld: warning: $WORKSPACE/nuttx/nuttx has a LOAD segment with RWX permissions
```

该 warning 未导致构建失败。

## 代码一致性

- `git diff --check` 通过，无残余冲突标记。
- 构建时团队仓库只有预期的 14 个 P1 修改文件。

## 范围限制

本次 **明确没有** 执行以下操作：

| 操作 | 状态 |
| --- | --- |
| SDK `rtt.bin` 替换 | 未执行 |
| `mkimage` FIT 包装 | 未执行 |
| `amp.img` 生成 | 未执行 |
| `update.img` 重新打包 | 未执行 |
| 板端烧录 | 未执行 |
| NSH prompt 验证 | 未执行 |
| `help` / `uname -a` / `ps` 验证 | 未执行 |
| UART RX/TX 板端验证 | 未执行 |

因此，**不得**将本记录视为 post-P0/P1 runtime 已验证的证据。

## 后续验证步骤

1. 执行 SDK `rtt.bin` 替换与 `mkimage` FIT 包装，生成 `amp.img`。
2. 完整烧录或仅 AMP 分区烧录至板端。
3. 通过 UART5（1.5M 8N1）连接串口，验证 NSH prompt、`help`、`uname -a`。
4. 如启用 PROCFS，验证 `ps` 输出。
5. 将板端验证 transcript 与本构建产物 hash 绑定归档。

## 与既有基线的关系

- 本记录是 P1 收敛阶段的 **build-only candidate**，不是板测基线。
- 不得复用 pre-P0/P1 基线（`8987bbc`）或 ec43ebb 复测记录中的 artifact hash 来代表本次构建。
- 只有在完成上述后续验证步骤后，本候选才能升级为板测基线。
