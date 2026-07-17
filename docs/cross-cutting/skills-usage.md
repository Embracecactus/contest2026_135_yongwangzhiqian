# Skills 使用说明（跨硬件）

`skills/hardware-adaptation/` 集合（5 Skills + 全局规则）应用于本仓库的多硬件适配。

## Skills 与硬件的对应

| Skill | RV1126B HPMCU | BK7258 T5-AI |
|---|---|---|
| `stage-restore-prompt` | ✅ 已用（8 个 next-stage prompt） | 待用 |
| `hardware-review-gate` | ✅ 已用（P2-A 四维评审） | 待用（bootloader 审查） |
| `firmware-gate` | ✅ 已用（SDK 打包链路） | 待用（bootloader + app 打包） |
| `board-verify` | ✅ 已用（mailbox/IRQ/channel 验证） | 待用（bootloader 跳转验证） |
| `hardware-context` | ✅ 已用（SDK 索引） | 待用（需迭代 SDK 结构适配） |

## 已知待迭代项

- `hardware-context/scripts/scan-sdk.sh` 当前只适配 RV1126B SDK 结构，BK7258（ARMINO）结构不同，需扩展扫描逻辑。
- `board-verify` 的连接模式（Manual/SSH/Serial/JLink/RTT）对 BK7258 同样适用。
