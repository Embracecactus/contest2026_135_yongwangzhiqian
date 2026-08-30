# T5AI-Core V1.0.1 裸机启动探针（历史证据）

> 本目录记录早期 BK7258 移植时使用的最小探针，不是当前产品构建、签名或下载入口。
> 当前下载必须遵守平台的
> [构建、发布与硬件证据 SOP](../../../nuttx-port/bk7258-build-flash-debug-sop.md)；
> 不得使用本页恢复旧 CRC 脚本、稀疏烧录命令或历史信任状态。

## 适用边界

- 物理板：涂鸦 T5AI-Core V1.0.1
- SoC：Beken BK7258
- 历史用途：验证 Bootloader 跳转落点、向量表、MSP、VTOR 和早期 UART 输出
- 不覆盖：当前 BL1/BL2 信任、A/B 更新、产品分区、其他 BK7258 板卡

探针链接到历史 app XIP 地址 `0x02010000`，在 `Reset_Handler` 中设置 VTOR，读取
辅助 core-id 字、`SCB->CPUID` 和 `SCB->VTOR`，随后通过 Bootloader 已配置的 UART1
轮询输出并停机。

## 保留文件

| 文件 | 说明 |
|---|---|
| `probe.c` | 66 项历史向量表、默认 handler、Reset_Handler 和轮询 UART 输出 |
| `probe.ld` | FLASH `0x02010000`、RAM `0x28000000` 的历史链接布局 |
| `Makefile` | 仅用于复核源码可构建；输出均为忽略的本地产物 |

这些文件的来源和许可证登记在 `SOURCE_PROVENANCE.md`。它们不由 NuttX/OpenVela
构建系统消费，也不应被加入当前包或 operator image。

## 历史镜像自检

当时接受的 raw `probe.bin` 为 620 bytes：

| 偏移 | 期望字节 | 含义 |
|---|---|---|
| `0x000` | `fc ff 09 28` | 初始 MSP `0x2809fffc`（小端） |
| `0x004` | `61 01 01 02` | 历史 Reset_Handler `0x02010161`，Thumb bit 为 1 |
| `0x100` | `42 4b 37 32 33 36 00 00` | 历史 app magic `BK7236\0\0` |

`probe.bin` 是逻辑视图；当时板载 Flash 使用 32-byte data + 2-byte CRC 的物理编码。
仓库当前编码与校验实现位于 `tools/bk7258/_lib/image.py`，但统一 CLI 没有把任意裸探针
变成可下载产品的公共命令，因此本页不再给出手工编码或烧录步骤。

## 历史输出与判读

当时的预期输出为：

```text
BK7258 PROBE
core=0x????????
cpuid=0x????????
vtor=0x02010000
HALT
```

- `core` 来自 `0x20000000` 的软件约定字，早期 Bootloader 未初始化时可能是任意值，
  不能单独证明执行核。
- `cpuid` 用于确认 Cortex-M33 实例。
- `vtor=0x02010000` 证明探针自己的向量表已接管；结合已分析的 Bootloader 默认
  handoff 路径，构成当时 CPU0 app 跳转证据。

完整板型边界见上级[硬件说明](../README.md)，当时的实板接受记录见
[2026-08-09 验证记录](../validation-2026-08-09.md)。当前启动事实仍须从
`chips/bk7258/`、构建 manifest 和匹配的日期化 verification 重新确认。
