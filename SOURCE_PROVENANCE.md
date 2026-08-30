<!-- SPDX-License-Identifier: Apache-2.0 -->
# 源码许可证与来源记录

## 审计范围

本记录覆盖 Git 已跟踪的 `*.c`、`*.cpp`、`*.h`、`*.S`、`*.s`、`*.ld`、
`*.py`、`*.sh` 和 `*.ps1` 编译/可执行源码，AI 对话日志不作为源码统计。
2026-08-30 按当前拟提交工作树（包含拟提交的未跟踪新增文件，排除删除项、`logs/`、
`memory/` 及忽略的 SDK/工具链/构建产物）复核结果为：

- 非测试源码 363 个，其中 362 个声明 Apache-2.0 SPDX；唯一例外是保持初始化原样的
  `app/hello_app/hello_app_main.c`，由仓库根 `LICENSE` 管理；
- `tests/host/bk7258/`、`app/testing/bk7258/` 和 `tests/pytest/` 共 164 个测试源码，
  Apache-2.0 SPDX 覆盖 164/164；
- 合计 527 个，Apache-2.0 SPDX 覆盖 526/527，另有上述一个明确模板例外。

本轮新增源码在创建时声明 SPDX；既有源码若只缺机器可读标识，则在保留原版权与
完整许可正文的前提下补齐。任何从 SDK 或外部仓提取的协议/初始化序列均在下表固定
仓库、版本、路径和许可证，不因改写为 NuttX 组织形式而省略来源。

## 来源分类

| 范围 | 来源与许可处理 |
|---|---|
| `app/hello_app/hello_app_main.c` | 来自本仓初始脚手架提交 `8987bbc`，并在 `7d9c26c` 统一为 team 135；本轮按该基线逐字恢复，不为许可证格式单独改写模板。仓库根 `LICENSE` 为 Apache-2.0。 |
| `app/bk7258/*.c` | 12 个 BK7258 命令均由本仓创建，原创建提交为 `38699e8`、`77ed92f`、`095b013`、`6fef975`、`36fc6a2`、`ecea356`、`7923cb4`、`c588afb`、`56d8cfd` 和 `0cc5ef7`；本轮从 hello 模板目录分离到独立产品应用，适用 Apache-2.0。 |
| `boards/bk7258/*/include/board.h` | 本仓提交 `eaef241` 创建的三个最小板级转发头，不复制其他 NuttX 板实现；适用仓库默认 Apache-2.0。 |
| `nuttx/drivers/lcd/gc9d01.c` | 初始化序列源自 Beken BK-AVDK v3.1.1.9 的 `components/bk_peripheral/src/lcd/spi/lcd_spi_gc9d01.c`（Apache-2.0）；本仓重写为传输无关、可上游化的 NuttX LCD 驱动，未暴露 SDK 私有面板对象。 |
| `nuttx/drivers/lcd/ili9488_rgb.c` | 初始化序列源自 `tuya/TuyaOpen-T5AI` 固定提交 `13379b63e07e78770fb4d0bffe36db2754658132` 的 `tuyaos/tuyaos_adapter/src/test/test_dvp/lcd_ill9488.c`（仓库根许可证 Apache-2.0）；本仓实现仅保留通用寄存器序列和传输回调，并复用官方 NuttX `ili9488.h` 命令定义。 |
| `chips/bk7258/bootloader/` | BL1、BL2、链接脚本及板级 MCUboot 配置/ABI 由本仓提交创建。BL2 在构建时链接工作区 `apps/boot/mcuboot/mcuboot` 的固定上游源码；仓内文件只是 BK7258 启动、Flash map、安全计数和最小配置适配，不包含上游 bootutil/TinyCrypt 实现副本。两侧均为 Apache-2.0。 |
| `chips/bk7258/common/bk7258_os_adapt.c` | 本仓面向 NuttX 编写的 SDK OS 适配层，文件原有完整 ASF Apache-2.0 许可正文；本轮仅增加 SPDX。 |
| `chips/bk7258/include/eth_mac*.h`、`lan8742.h` | 来自 manifest 固定的 Beken SDK v3.1.1.9 Ethernet 公开头。原文件保留 Beken 版权和完整 Apache-2.0 正文；其中 `lan8742.h` 与 SDK 相同，其余仅有换行或已注明的 NuttX 符号兼容调整。 |
| `docs/platforms/bk7258/hardware/t5ai-core/probe/*.{c,ld}` | 本仓提交 `56b303e` 创建的历史实板探针源码，适用仓库默认 Apache-2.0。 |
| `boards/bk7258/build/vendorsetup.sh` | 本仓提交 `eaef241` 创建的构建环境适配脚本；随完整板目录映射进入 OpenVela 的 vendor 树，适用仓库默认 Apache-2.0。 |
| `tools/windows-hardware-debug/**/*.{cpp,ps1}` | 本仓硬件调试工具，由 2026-07-31 至 2026-08-03 的调试与 BLE 验证提交创建；10 个文件在本轮前已声明 Apache-2.0 SPDX。 |

Beken SDK 由 [`contest2026_135_yongwangzhiqian.xml`](contest2026_135_yongwangzhiqian.xml)
固定在提交 `cb080de1655d579c7593ecf504c440997c4c137b`，其根 `LICENSE` 和上述
Ethernet 公开头均声明 Apache-2.0。MCUboot 由 openvela 工作区的 `apps` 项目提供，
其上游目录保留独立 `LICENSE` 和 `NOTICE`。

BK7258 主机测试的更细分类见
[`tests/host/bk7258/PROVENANCE.md`](tests/host/bk7258/PROVENANCE.md)。第三方项目、预构建工具、
生成输出及历史材料继续适用各自声明；SPDX 补齐不改变其版权归属。
