<!-- SPDX-License-Identifier: Apache-2.0 -->
# BK7258 主机测试源码许可证与来源

## 审计范围

本记录覆盖 `tests/host/bk7258/` 下 Git 已跟踪的 `*.c`、`*.h`、`*.py`、`*.sh`
源码。2026-08-30 按当前拟提交工作树逐文件复核共 161 个，全部标记
`SPDX-License-Identifier: Apache-2.0`。仓库级许可证全文见 `LICENSE`。

Git 创建历史显示，这些文件均由本仓提交直接加入；未发现由本仓其他路径重命名或复制
而来的记录。SPDX 标记说明本仓测试实现的授权，不取代下述上游项目原有的版权和许可
声明。

## 来源分类

| 范围 | 来源与处理 | 许可证依据 |
|---|---|---|
| 测试用例、`framework/`、`modules/`、`mocks/arch/chip/` 及主机 mock 实现 | 为本仓 BK7258 适配编写的测试逻辑、故障注入和最小替身 | 本仓 Apache-2.0 |
| `mocks/common/`、`mocks/components/`、`mocks/driver/` 及 SDK mock 声明 | 与 manifest 固定的 Beken BK7258 SDK v3.1.1.9 最小 ABI 对齐，不包含 SDK 生产实现 | SDK 提交 `cb080de1655d579c7593ecf504c440997c4c137b` 的根 `LICENSE` 和相应公开头均为 Apache-2.0 |
| `mocks/nuttx/`、`mocks/nuttx_can/`、`mocks/nuttx_yuv/` | 为主机编译抽取的最小 NuttX ABI 替身，不是 NuttX 实现副本 | Apache NuttX 对应公开头为 Apache-2.0，并保留其上游 `LICENSE`/`NOTICE` |
| `mocks/bootutil/`、`mocks/tinycrypt/` | 本地测试专用的最小常量和函数声明，用于隔离 MCUboot/TinyCrypt 依赖；不包含密码算法实现 | 本仓测试替身按 Apache-2.0 发布；实际依赖仍适用各自上游声明 |
| `mocks/lvgl/lvgl.h` | 仅含两个不透明类型的前置声明，为本仓主机编译替身，不包含 LVGL 实现 | 本仓 Apache-2.0 |
| `framework/mock_bl1_public_key.c` | 确定性递增字节公钥夹具及其哈希，不是私钥、下载密钥或可部署信任根 | 本仓 Apache-2.0 |

SDK 版本来源由仓库根目录 `contest2026_135_yongwangzhiqian.xml`
固定。NuttX 由 `openvela.xml` 纳入工作区，测试仅针对当前工作区的
公开 ABI 编译。

## 生成物边界

`tests/host/bk7258/build/` 下的临时源码、分区头、目标文件和可执行文件由测试入口生成且不
纳入 Git；它们继承其输入源码或生成器的许可证。新增或替换 mock 时，应在评审中重新
确认来源，不能仅因位于本目录而机械沿用许可证。
