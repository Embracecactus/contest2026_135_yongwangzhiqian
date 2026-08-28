# 2026-07-16 RV1126B P1 AMP FIT packaging 记录

> **历史 RV1126B 证据：**当前参赛主线为 BK7258；本记录只保留当日构建或板测事实，
> 不代表当前实现、活动配置或下一步计划。

> 本记录归档 P1 收敛候选的 AMP FIT 打包证据。打包前的源 build 记录见 [2026-07-16-rv1126b-p1-convergence-build.md](./2026-07-16-rv1126b-p1-convergence-build.md)。本次未执行 `update.img` 重新打包、烧录、或任何板端 NSH/UART 验证。

## 结论

| 检查项 | 状态 |
| --- | --- |
| 源 build（classic Make） | 已验证，见收敛构建记录 |
| AMP FIT 打包（mkimage） | **已验证**，exit code 0，产物 hash 已记录 |
| `update.img` 重新打包 | 未执行 |
| 板端烧录 | 未执行 |
| NSH prompt / UART runtime | 未执行 |

**build + packaging 已验证；runtime 未验证。** hash 仅标识本次产物，不证明可复现性。

## 路径约定

本文档使用以下路径变量，不包含个人绝对路径：

```text
WORKSPACE=/absolute/path/to/open-vela
CONTEST=$WORKSPACE/contest2026_135_yongwangzhiqian
SDK=/absolute/path/to/rv1126b-sdk
OUT=$SDK/output
FW=$OUT/firmware
```

## 输入 provenance

打包输入来自同一次 P1 收敛构建：

| 来源 | 路径 | 大小 | sha256 |
| --- | --- | ---: | --- |
| 构建产物 | `$WORKSPACE/nuttx/nuttx.bin` | 98820 bytes | `26f4ae0ea2fc4398054c814bd2d79304c62ba1b530ec992835af9133ce9dbf00` |
| SDK 原始 RTT target | `$SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/rttmcu.bin` | 98728 bytes | `63286394f011246de1669da5016389ee5c5e6f74da577d54d594465c665590ca` |
| 旧 AMP image | `$FW/amp.img` | 100406 bytes | `f82b910e3d72e58c4f3e2dccb3a67f9ed5e740ffa209ebc188630e1b149f8dc7` |

`$OUT/rtt.bin` 为 symlink，链接文本为 `../rtos/bsp/rockchip/rv1126b-mcu/Image/rttmcu.bin`。

## 备份

替换前已创建备份：

| 备份文件 | 原始 hash |
| --- | --- |
| `$SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/rttmcu.bin.pre-p1-20260716` | `63286394f011246de1669da5016389ee5c5e6f74da577d54d594465c665590ca` |
| `$FW/amp.img.pre-p1-20260716` | `f82b910e3d72e58c4f3e2dccb3a67f9ed5e740ffa209ebc188630e1b149f8dc7` |

## symlink-safe 替换

1. 将 `$WORKSPACE/nuttx/nuttx.bin` 复制到 `$SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/rttmcu.bin`（替换真实 target 文件，不修改 symlink）。
2. 替换后验证：大小 98820 bytes，sha256 与 SOURCE 一致（`26f4ae0ea2fc4398054c814bd2d79304c62ba1b530ec992835af9133ce9dbf00`）。
3. `$OUT/rtt.bin` symlink 未变，仍指向 `../rtos/bsp/rockchip/rv1126b-mcu/Image/rttmcu.bin`。

## mkimage 命令与完整关键输出

从 `$OUT` 执行：

```bash
$SDK/rtos/bsp/rockchip/tools/mkimage -f amp.its -E -p 0xe00 $FW/amp.img
```

exit code：**0**

关键输出：

```text
FIT description: FIT source file for rockchip AMP
Created: Thu Jul 16 09:59:05 2026
Image 0 (hpmcu), Standalone Program, uncompressed
  Data Size: 98820 Bytes = 96.50 KiB = 0.09 MiB
  Architecture: ARM
  Load Address: 0x48c02000
  Entry Point: unavailable
  Hash algo sha256
  Hash value 26f4ae0ea2fc4398054c814bd2d79304c62ba1b530ec992835af9133ce9dbf00
Default Configuration conf; Loadables hpmcu
```

> **Architecture ARM 说明：** 这是 Rockchip FIT/boot chain 的既有封装约定，不代表 payload 是 ARM 代码。payload 来源是 RV1126B HPMCU RISC-V `nuttx.bin`。

同一 mkimage `-l` 列表验证成功，内容与上述一致。

## 最终产物

| 产物 | 路径 | 大小 | mtime | sha256 |
| --- | --- | ---: | --- | --- |
| 新 AMP image | `$FW/amp.img` | 103936 bytes | 2026-07-16 09:59:05 | `585602012d9af4d3ba8980b7922d7a3273a4b1edcae212475b180b0f54b425e9` |

## 范围限制

本次 **明确没有** 执行以下操作：

| 操作 | 状态 |
| --- | --- |
| `update.img` 重新打包 | 未执行 |
| `upgrade_tool` 烧录 | 未执行 |
| `rkdeveloptool` 烧录 | 未执行 |
| 任何形式的 flash | 未执行 |
| 板端 NSH prompt | 未执行 |
| `help` / `uname -a` / `ps` | 未执行 |
| UART RX/TX 板端验证 | 未执行 |

因此，**不得**将本记录视为 runtime 已验证的证据。

## 下一步

1. 使用单独确认的精确命令仅烧录 AMP 分区；完整 `update.img` 另行验证。
2. 通过 UART5（1.5M 8N1）连接串口，验证 NSH prompt、`help`、`uname -a`。
3. 如启用 PROCFS，验证 `ps` 输出。
4. 将板端验证 transcript 与本记录的 amp.img hash 绑定归档。
