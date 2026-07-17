# BK7258 T5-AI 适配

涂鸦 T5-AI 开发板，基于博通 BK7258（ARM Cortex-M33，三核，Wi-Fi 6 + BLE 5.4）。

## 当前阶段

**技术探索**：bootloader 逆向 + NuttX SMP 可行性分析

## 关键资源

| 资源 | 路径 |
|---|---|
| Beken ARMINO SDK | `/home/lijian/project/armino/bk_avdk_smp` |
| Tuya SDK | `/home/lijian/project/TuyaOpen/TuyaOpen` |
| 已有 Zephyr port（含 bootloader） | `/home/lijian/project/TuyaOpen/zephyr-bk7258-port` |
| 涂鸦 bootloader（65KB） | `zephyr-bk7258-port/tools/t5ai_bootloader.bin` |
| BK 官方 bootloader（52KB） | `bk_avdk_smp/cp/components/bk_libs/bk7258/bootloader/normal_bootloader/bootloader.bin` |

## 阻塞项

- [ ] bootloader 逆向：涂鸦 vs BK 官方差异分析（magic 偏移 0x110 vs 0x100）
- [ ] bootloader 设计：CRC 扩展 + 头部校验，让 NuttX bin 直接可用
- [ ] NuttX BSP 骨架：链接脚本（0x02010000）、向量表、Magic

## 子目录

- [bootloader/](bootloader/) — bootloader 逆向分析与实现
- [nuttx-port/](nuttx-port/) — NuttX 移植
