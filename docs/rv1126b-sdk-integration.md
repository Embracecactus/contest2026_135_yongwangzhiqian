# RV1126B SDK 侧集成与可复现打包说明

## 目的与范围

本文记录 RV1126B 自有板 HPMCU 侧 openvela / NuttX 镜像在 Rockchip / ATK SDK 中完成 AMP 打包、`update.img` 重新生成和板端验证所需的 SDK 侧前置条件。

本文只描述 SDK 侧复现环境与 patch 清单边界：

- contest 源码仍以 `$CONTEST` 中的团队 overlay 为准。
- SDK 侧 DTS、AMP 配置和 package-file 属于板级复现输入，不作为 contest 仓主体源码提交。
- 本文不包含外部 SDK 的精确 diff hunk；当前只整理已有验证记录中已经确认的路径、命令和缺口。
- 2026-07-15 已验证的是 **AMP 分区更新**启动到 NSH；完整 `update.img` 已重新生成，但尚未整包刷写验证。

## 路径约定

后续命令统一使用可替换变量，不记录个人电脑绝对路径：

```bash
export WORKSPACE=/absolute/path/to/open-vela
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export SDK=/absolute/path/to/rv1126b-sdk
export OUT="$SDK/output"
export FW="$SDK/output/firmware"
```

## openvela 构建前置条件

当前可复现的 HPMCU 镜像需要 contest board defconfig 能稳定生成 raw binary 与 Intel HEX：

```text
CONFIG_RAW_BINARY=y
CONFIG_INTELHEX_BINARY=y
```

已验证的构建后端是 classic Make；不要把 CMake 构建视为等价验证结果。

```bash
cd "$WORKSPACE"
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
```

2026-07-15 验证记录中的 openvela / NuttX 构建产物身份如下：

| 产物 | 大小 | sha256 |
| --- | ---: | --- |
| `$WORKSPACE/nuttx/nuttx` | 154K | `e31c4c712514f382e6602a9b26db404117706c15477c21c4922e3468750bd695` |
| `$WORKSPACE/nuttx/nuttx.bin` | 79K / 80320 bytes | `0b52cefbd9ca2cf974543300dd61feca56d4989353b7737cbeb7d0c176017075` |
| `$WORKSPACE/nuttx/nuttx.hex` | 221K | `91aa83ec935d306d7eae4d7cfe7c88f87e9d7961e8f44423e2d3e8be0c05bada` |

## SDK 侧前置条件 / patch 清单

当前已知 SDK 侧必须满足以下条件，才能复现 UART5 控制台与 AMP 启动链路。

### UART5 资源避让

Linux A 核不能占用 HPMCU 控制台使用的 UART5 M0，否则 HPMCU 侧 NSH 串口会与 Linux 侧资源冲突。

HPMCU 控制台参数：

```text
UART: UART5 M0
Pins: GPIO4_PA6 / GPIO4_PA7
Function: FUNC5
Baudrate: 1500000
Format: 8N1
Flow control: none
```

### 已知涉及的 SDK 文件

以下路径来自当前验证文档和历史调研。它们应被视为 SDK 侧 patch / 复现清单，而不是 contest 仓源码：

| SDK 路径 | 作用 | 当前记录状态 |
| --- | --- | --- |
| `$SDK/kernel-6.1/arch/arm64/boot/dts/rockchip/rv1126b-sportcam.dts` | 自有板 / SportCam 板级 DTS | 已知需要参与 UART5 资源避让和板级配置 |
| `$SDK/kernel-6.1/arch/arm64/boot/dts/rockchip/FET1126B-S.dtsi` | 自有板公共 DTSI | 已知与板级资源描述相关 |
| `$SDK/kernel-6.1/arch/arm64/boot/dts/rockchip/rv1126b-amp.dtsi` | RV1126B AMP / HPMCU 配置 | 已知与 AMP 启动、HPMCU 内存和资源描述相关 |
| `$SDK/device/rockchip/.chips/rv1126b/amp_mcu.its` | SDK AMP MCU 镜像配置入口 | 需要选择自有板 / SportCam AMP 配置；当前记录提到已切到 `board/rv1126b_sportcam/defconfig` |
| `$SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/amp.its` | RT-Thread BSP 原始 AMP FIT 参考 | 历史流程参考；不是 2026-07-15 已验证主打包路径 |
| `$SDK/rtos/bsp/rockchip/rv1126b-mcu/link.lds` | HPMCU 内存布局参考 | 记录 HPMCU load address / RAM window |
| `$OUT/amp.its` | 2026-07-15 已验证 FIT 描述 | 当前主流程使用 |
| SDK package-file | `update.img` 分区打包清单 | 需要包含 `amp    amp.img`；精确文件路径待补 |

> [!NOTE]
> 本文未读取外部 SDK，因此不虚构 DTS、`amp_mcu.its` 或 package-file 的精确 diff hunk。后续如果需要提交 SDK patch 包，应在用户确认后只读检查 SDK 并补齐精确路径与 diff。

## 2026-07-15 已验证 AMP 打包主流程

当前主流程以 2026-07-15 验证记录为准：

```text
$WORKSPACE/nuttx/nuttx.bin
  -> $OUT/rtt.bin
  -> mkimage + $OUT/amp.its
  -> $FW/amp.img
  -> ./build.sh updateimg
  -> $SDK/output/update/Image/update.img
```

不要把历史文档中的旧流程与本流程混用。旧流程中曾出现 `$SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/rtt.bin`、`$SDK/hal/tools/mkimage` 或中间产物 `Image/nuttx_amp.img`，这些只作为历史参考；本次板测通过的是 `$OUT/rtt.bin` 与 `$OUT/amp.its` 流程。

### 替换并生成 `amp.img`

```bash
export WORKSPACE=/absolute/path/to/open-vela
export SDK=/absolute/path/to/rv1126b-sdk
export OUT="$SDK/output"
export FW="$SDK/output/firmware"

cp -av "$OUT/rtt.bin" "$OUT/rtt.bin.before-openvela-ec43ebb" 2>/dev/null || true
cp -av "$FW/amp.img" "$FW/amp.img.before-openvela-ec43ebb"

cp -av "$WORKSPACE/nuttx/nuttx.bin" "$OUT/rtt.bin"
sha256sum "$OUT/rtt.bin"

cd "$OUT"
"$SDK/rtos/bsp/rockchip/tools/mkimage" -f amp.its -E -p 0xe00 "$FW/amp.img"

ls -lh "$FW/amp.img"
sha256sum "$FW/amp.img"
```

`$OUT/amp.its` 中的关键事实：

```dts
images {
    hpmcu {
        description  = "hpmcu";
        data         = /incbin/("rtt.bin");
        type         = "standalone";
        compression  = "none";
        arch         = "arm"; /* actually RISC-V, but U-Boot only accepts arm/arm64 */
        udelay       = <10000>;
        load         = <0x48c02000>;
    };
};
```

`mkimage` 输出中的关键证据：

```text
FIT description: FIT source file for rockchip AMP
Image 0 (hpmcu)
  Data Size:    80320 Bytes = 78.44 KiB = 0.08 MiB
  Architecture: ARM
  Load Address: 0x48c02000
  Hash algo:    sha256
  Hash value:   0b52cefbd9ca2cf974543300dd61feca56d4989353b7737cbeb7d0c176017075
```

生成的 AMP 镜像身份：

| 产物 | 大小 | sha256 |
| --- | ---: | --- |
| `$FW/amp.img` | 83K | `35f501c77ac77a27437160b8de190e9245ef736ee5b1d1d89634d28592618feb` |

## 重新生成 `update.img`

SDK package-file 中必须包含 AMP 分区条目：

```text
amp    amp.img
```

重新打包命令：

```bash
cd "$SDK"
./build.sh updateimg
```

打包日志中需要确认 `amp.img` 被加入：

```text
Add file: ./amp.img
amp,Add file: ./amp.img done,offset=0x2c05000,size=0x14c00,userspace=0x2a,flash_address=0x00028000
Make firmware OK!
New image generated successfully!
```

生成的完整升级包身份：

| 产物 | 大小 | sha256 |
| --- | ---: | --- |
| `$SDK/output/update/Image/update.img` | 1.4G | `89171c596e0df878ab0efa44d0f22007decc537853dae08281fee649fbe628a0` |

同时记录到的软链：

```text
$FW/update.img -> ../update/Image/update.img
```

## 板端验证边界

2026-07-15 板测采用的是只更新 AMP 分区对应 `amp.img` 的方式，用来验证 HPMCU openvela / NuttX 镜像本身是否能启动并交互。

当前可以声明：

- `$WORKSPACE/nuttx/nuttx.bin` 可被包装为 `$FW/amp.img`。
- `mkimage` hash 与 `nuttx.bin` hash 一致。
- `./build.sh updateimg` 可重新生成包含当前 `amp.img` 的 `update.img`。
- 只更新 AMP 分区后，板端可以进入 NSH，UART RX/TX、`uname -a` 与 `help` 通过。

当前不能声明：

- 完整 `update.img` 已整包刷写并通过。
- `ps` 已启用或通过。
- RPMsg / Linux A-core 与 HPMCU 通信已完成。
- CMake 构建与 classic Make 等价。
- DCache 已启用或完成验证。

串口验证摘录：

```text
nsh> uname -a
NuttX 0.0.0 e02f581e23 Jul 15 2026 14:00:58 risc-v rv1126b_evb
nsh> help
... command list printed ...
nsh> ps
nsh: ps: command not found
nsh>
```

`ps: command not found` 是当前最小 defconfig 未启用 `CONFIG_FS_PROCFS` 的已知结果，不作为 L0 基线失败项。

## 后续待补

1. 只读确认外部 SDK 后，补齐 SDK patch 的精确 diff hunk。
2. 补齐包含 `amp    amp.img` 的 package-file 精确路径。
3. 补齐 2026-07-15 实际 AMP 分区烧录命令；历史候选命令包括 `upgrade_tool di amp amp.img`，但本文暂不把它标为本次已捕获命令。
4. 发布前可安排一次完整 `update.img` 整包刷写回归，并记录日志、命令和 artifact hash。
