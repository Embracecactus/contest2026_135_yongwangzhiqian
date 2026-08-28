# 2026-07-15 RV1126B 当前提交 AMP 分区 NSH 复测记录

> **历史 RV1126B 证据：**当前参赛主线为 BK7258；本记录只保留当日构建或板测事实，
> 不代表当前实现、活动配置或下一步计划。

## 结论

本次复测证明：基于当前 contest 仓提交 `ec43ebb` 构建出的 openvela / NuttX HPMCU 镜像，可以通过 Rockchip / ATK SDK 的 AMP FIT 包装链路生成 `amp.img`，并在 RV1126B 自有板上通过**只更新 AMP 分区**的方式启动到 NSH，完成 UART 控制台交互、`uname -a` 与 `help` 验证。

本次没有完整烧录 `update.img`，因为完整刷写耗时较长；但已验证 `update.img` 可由包含当前 `amp.img` 的 SDK 输出重新打包生成。

## 范围与边界

- Contest 仓库：`$CONTEST`
- Contest HEAD：`ec43ebb`
- 当前工作区状态：
  - `README.md` 有未提交修改：根 README 暂时保留/回到官方指导模板，最终提交前再改成作品说明。
  - `board/contest_board/configs/nsh/defconfig` 有未提交修改：新增 `CONFIG_INTELHEX_BINARY=y` 与 `CONFIG_RAW_BINARY=y`，用于稳定生成 `nuttx.hex` / `nuttx.bin`。
- 本次只验证 HPMCU AMP 镜像替换链路，不声明完整 `update.img` 已整包刷写。
- `ps` 验证尝试过，但最终未作为本次基线条件：当前最小 defconfig 未启用 `CONFIG_FS_PROCFS`，`ps` 不是当前 L0 基线通过项。

## openvela / NuttX 构建产物

构建后产物：

| 产物 | 大小 | sha256 |
| --- | ---: | --- |
| `$WORKSPACE/nuttx/nuttx` | 154K | `e31c4c712514f382e6602a9b26db404117706c15477c21c4922e3468750bd695` |
| `$WORKSPACE/nuttx/nuttx.bin` | 79K | `0b52cefbd9ca2cf974543300dd61feca56d4989353b7737cbeb7d0c176017075` |
| `$WORKSPACE/nuttx/nuttx.hex` | 221K | `91aa83ec935d306d7eae4d7cfe7c88f87e9d7961e8f44423e2d3e8be0c05bada` |

`nuttx.bin` 大小为 80320 bytes，对应后续 AMP FIT 中的 `hpmcu` data。

## SDK 前置环境

SDK 路径：

```text
$SDK
```

SDK 已经完整编译过，本次复测没有从头重新编译 SDK / Linux / U-Boot / Buildroot；只替换 HPMCU 镜像并重新打包。

SDK 侧存在必要板级前置修改：

- Linux A 核 DTS / board 配置需要避让 UART5，避免与 HPMCU 控制台冲突。
- RV1126B HPMCU 使用 UART5 M0：GPIO4_PA6 / GPIO4_PA7，FUNC5，1.5M 8N1。
- `device/rockchip/.chips/rv1126b/amp_mcu.its` 已切到 `board/rv1126b_sportcam/defconfig`。
- `kernel-6.1` 侧存在 `rv1126b-sportcam.dts` / `FET1126B-S.dtsi` / `rv1126b-amp.dtsi` 等自有板与 AMP 相关修改。

这些 SDK 修改不属于 contest 仓主体代码，但属于本板可复现烧录/运行环境，应在最终作品文档中说明或以 patch 形式归档。

## AMP FIT 包装

当前 `output/amp.its` 的关键内容：

```dts
images {
    hpmcu {
        description  = "hpmcu";
        data         = /incbin/("rtt.bin");
        type         = "standalone";
        compression  = "none";
        arch         = "arm"; // Actually it's riscv, but uboot only accept arm/arm64
        udelay       = <10000>;
        load         = <0x48c02000>;
    };
};
```

本次替换与打包命令：

```bash
SDK=$SDK
OUT=$SDK/output
FW=$SDK/output/firmware

cp -av "$OUT/rtt.bin" "$OUT/rtt.bin.before-openvela-ec43ebb" 2>/dev/null || true
cp -av "$FW/amp.img" "$FW/amp.img.before-openvela-ec43ebb"

cp -av $WORKSPACE/nuttx/nuttx.bin "$OUT/rtt.bin"
sha256sum "$OUT/rtt.bin"

cd "$OUT"
"$SDK/rtos/bsp/rockchip/tools/mkimage" -f amp.its -E -p 0xe00 "$FW/amp.img"

ls -lh "$FW/amp.img"
sha256sum "$FW/amp.img"
```

`mkimage` 输出要点：

```text
FIT description: FIT source file for rockchip AMP
Image 0 (hpmcu)
  Data Size:    80320 Bytes = 78.44 KiB = 0.08 MiB
  Architecture: ARM
  Load Address: 0x48c02000
  Hash algo:    sha256
  Hash value:   0b52cefbd9ca2cf974543300dd61feca56d4989353b7737cbeb7d0c176017075
```

生成的 `amp.img`：

| 产物 | 大小 | sha256 |
| --- | ---: | --- |
| `$SDK/output/firmware/amp.img` | 83K | `35f501c77ac77a27437160b8de190e9245ef736ee5b1d1d89634d28592618feb` |

## update.img 重新打包

重新打包命令：

```bash
cd $SDK
./build.sh updateimg
```

`package-file` 中 AMP 分区条目：

```text
amp    amp.img
```

打包日志确认 `amp.img` 被加入：

```text
Add file: ./amp.img
amp,Add file: ./amp.img done,offset=0x2c05000,size=0x14c00,userspace=0x2a,flash_address=0x00028000
Make firmware OK!
New image generated successfully!
```

生成的 `update.img`：

| 产物 | 大小 | sha256 |
| --- | ---: | --- |
| `$SDK/output/update/Image/update.img` | 1.4G | `89171c596e0df878ab0efa44d0f22007decc537853dae08281fee649fbe628a0` |

软链：

```text
output/firmware/update.img -> ../update/Image/update.img
```

## 板测方式

本次为了节省时间，只更新/烧录了 AMP 分区对应的 `amp.img`，没有完整烧录 `update.img`。

该方式用于验证 HPMCU openvela / NuttX 镜像本身是否能启动与交互；完整 `update.img` 刷写可作为后续发布前回归。

## 串口参数

```text
UART: UART5 M0
Pins: GPIO4_PA6 / GPIO4_PA7
Function: FUNC5
Baudrate: 1500000
Format: 8N1
Flow control: none
```

## 串口验证记录

```text
nsh> uname -a
NuttX 0.0.0 e02f581e23 Jul 15 2026 14:00:58 risc-v rv1126b_evb
nsh> help
help usage:  help [-v] [<cmd>]

    .           cd          exit        mkrd        sleep       unset
    [           cp          expr        mount       source      uptime
    ?           cmp         false       mv          test        usleep
    alias       dirname     help        printf      time        watch
    unalias     dd          hexdump     pwd         true        xd
    basename    dmesg       kill        rm          truncate
    break       echo        ls          rmdir       uname
    cat         exec        mkdir       set         umount
nsh> ps
nsh: ps: command not found
nsh>
```

## 验收矩阵

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| 当前 `nuttx.bin` 生成 | 通过 | `CONFIG_RAW_BINARY=y` 已生效 |
| `nuttx.hex` 生成 | 通过 | `CONFIG_INTELHEX_BINARY=y` 已生效 |
| `nuttx.bin` 替换为 SDK `output/rtt.bin` | 通过 | sha256 一致 |
| `amp.img` FIT 包装 | 通过 | `mkimage` hash 指向当前 `nuttx.bin` |
| `update.img` 重新打包 | 通过 | `amp.img` 已加入 package |
| 完整 `update.img` 烧录 | 未执行 | 完整刷写耗时，后续发布前可补 |
| 仅 AMP 分区烧录 | 通过 | 当前板测采用该方式 |
| NSH prompt | 通过 | 串口可交互 |
| `uname -a` | 通过 | 显示 2026-07-15 14:00:58 构建 |
| `help` | 通过 | 命令表可输出 |
| `ps` | 未启用 | 当前最小 defconfig 未启用 PROCFS；不作为本次 L0 基线通过项 |

## 后续建议

1. 提交前可选择是否启用 `CONFIG_FS_PROCFS=y` 与 `ps`，但这会增加配置面；当前已回退到不以 `ps` 为基线。
2. 最终 README 再替换官方模板，避免开发阶段丢失官方指导。
3. 发布前可补一次完整 `update.img` 整包烧录回归。
4. SDK 侧 DTS / SportCam 修改需要整理为文档或 patch，避免评委无法复现 UART5 资源避让与 HPMCU 启动链路。
