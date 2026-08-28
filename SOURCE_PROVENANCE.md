<!-- SPDX-License-Identifier: Apache-2.0 -->
# 源码许可证与来源记录

## 审计范围

本记录覆盖 Git 已跟踪的 `*.c`、`*.cpp`、`*.h`、`*.S`、`*.s`、`*.ld`、
`*.py`、`*.sh` 和 `*.ps1` 编译/可执行源码，AI 对话日志不作为源码统计。
2026-08-28 复核结果为：

- 非测试源码 341 个，Apache-2.0 SPDX 覆盖 341/341；
- `tests/bk7258/` 源码 160 个，Apache-2.0 SPDX 覆盖 160/160；
- 合计 501 个，覆盖 501/501。

本轮补齐的 39 个非测试文件中，33 个由本仓提交直接创建；另外 6 个原本已经包含
Apache-2.0 完整许可正文，只缺机器可读的 SPDX 标识。逐文件 Git 创建历史没有发现
从本仓其他路径重命名或复制而来的记录。

## 来源分类

| 范围 | 来源与许可处理 |
|---|---|
| `app/hello_app/*.c` | `hello_app_main.c` 来自本仓初始脚手架提交 `8987bbc`，其余三个 BK7258 验证入口由本仓提交 `401f836` 创建；均适用仓库默认 Apache-2.0。 |
| `boards/bk7258/*/include/board.h` | 本仓提交 `eaef241` 创建的三个最小板级转发头，不复制其他 NuttX 板实现；适用仓库默认 Apache-2.0。 |
| `chips/bk7258/bootloader/` | BL1、BL2、链接脚本及板级 MCUboot 配置/ABI 由本仓提交创建。BL2 在构建时链接工作区 `apps/boot/mcuboot/mcuboot` 的固定上游源码；仓内文件只是 BK7258 启动、Flash map、安全计数和最小配置适配，不包含上游 bootutil/TinyCrypt 实现副本。两侧均为 Apache-2.0。 |
| `chips/bk7258/common/bk7258_os_adapt.c` | 本仓面向 NuttX 编写的 SDK OS 适配层，文件原有完整 ASF Apache-2.0 许可正文；本轮仅增加 SPDX。 |
| `chips/bk7258/include/eth_mac*.h`、`lan8742.h` | 来自 manifest 固定的 Beken SDK v3.1.1.9 Ethernet 公开头。原文件保留 Beken 版权和完整 Apache-2.0 正文；其中 `lan8742.h` 与 SDK 相同，其余仅有换行或已注明的 NuttX 符号兼容调整。 |
| `docs/platforms/bk7258/hardware/t5ai-core/probe/*.{c,ld}` | 本仓提交 `56b303e` 创建的历史实板探针源码，适用仓库默认 Apache-2.0。 |
| `integration/beken/vendorsetup.sh` | 本仓提交 `eaef241` 创建的构建环境适配脚本，适用仓库默认 Apache-2.0。 |
| `tools/windows-hardware-debug/**/*.{cpp,ps1}` | 本仓硬件调试工具，由 2026-07-31 至 2026-08-03 的调试与 BLE 验证提交创建；10 个文件在本轮前已声明 Apache-2.0 SPDX。 |

Beken SDK 由 [`contest2026_135_yongwangzhiqian.xml`](contest2026_135_yongwangzhiqian.xml)
固定在提交 `cb080de1655d579c7593ecf504c440997c4c137b`，其根 `LICENSE` 和上述
Ethernet 公开头均声明 Apache-2.0。MCUboot 由 openvela 工作区的 `apps` 项目提供，
其上游目录保留独立 `LICENSE` 和 `NOTICE`。

BK7258 主机测试的更细分类见
[`tests/bk7258/PROVENANCE.md`](tests/bk7258/PROVENANCE.md)。第三方项目、预构建工具、
生成输出及历史材料继续适用各自声明；SPDX 补齐不改变其版权归属。
