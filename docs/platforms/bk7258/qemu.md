# BK7258 QEMU 诊断接入

此入口只运行独立 QEMU 仓里的原生 SoC/device/machine 模型，不把固件 C 文件当作
QEMU 设备编译，也不向官方 NuttX 或已有 QEMU prebuilts 注入源文件。
模型的寄存器来源、覆盖与缺失项以 QEMU 仓 `docs/system/arm/bk7258.rst` 为准。

## 本地复现

宿主需要 GCC、GLib 开发包、Python、Meson、Ninja，以及用于小型裸机 fixture 的
Arm GNU 编译器。这个 fixture 编译器是显式输入，不修改产品工具链 pin，也不生成
可烧录或签名发布包。所有命令可从源码目录外调用：

```sh
python3 tools/bk7258/bk7258.py qemu build \
  --source /path/to/qemu-bk7258 --build-dir /path/to/out/qemu --jobs 6
python3 tools/bk7258/bk7258.py qemu smoke \
  --qemu /path/to/out/qemu/qemu-system-arm \
  --cc /path/to/arm-none-eabi-gcc --output /path/to/out/qemu-smoke
```

默认按 `boards/bk7258/*/openvela.conf` 发现板名；`smoke --board t5_board` 可限定
一块板。输出包括 ELF、各板正反例日志、未实现 MMIO 日志、编译器版本、模拟器和
ELF SHA-256，以及 `evidence.json`。超时、错误退出、缺少成功标记或未实现 MMIO
都会使 smoke 失败。

fixture 验证 UART、SRAM 四别名、内部及外部参考 SysTick 中断、CPU1/CPU2 释放、
每核 TCM、halt/resume、reset、UART RX IRQ 及完整系统重启。输入 `X` 必须进入
失败分支；输入 `Z` 才能成功。这不是 NuttX、NSH、AP 双核 SMP 或生产 CP/AP
启动验收。三个 machine 当前使用相同基础模型，各板独有外围尚未实现。

可单独重跑 smoke 生成的可信诊断 ELF：

```sh
printf Z | python3 tools/bk7258/bk7258.py qemu run --board t5_board \
  --qemu /path/to/out/qemu/qemu-system-arm --elf /path/to/diagnostic.elf \
  --semihosting
```

入口要求 ELF，拒绝原始 signed/CRC Flash BIN。CPU0 向量固定为 `0x02010000`，
直接跳过 ROM、BL1、BL2。`run` 默认关闭 semihosting；`--semihosting` 只供可信
诊断镜像，允许 guest 访问宿主文件。smoke 使用项目内固定 fixture 的 semihosting
退出码，不测试 Flash 持久化、MCUboot、OTA、TrustZone 安全归属或无线/音视频。

额外宿主契约测试：

```sh
BK7258_QEMU=/path/to/out/qemu/qemu-system-arm \
  python3 tests/host/bk7258/test_bk7258_qemu.py -v
```

## Manifest 同步

团队 manifest 已固定一个已发布 QEMU 提交，使用可选组 `bk7258-qemu`。在现有
工作区启用该组后，只同步 `external/qemu-bk7258` 即可；无需重新同步全部固件依赖。
标准 `repo init` 参数中的 group 应保留原有选择并追加 `bk7258-qemu`，随后执行
`repo sync external/qemu-bk7258`。项目不属于默认组，普通固件构建不会额外下载整份
QEMU 源码。

## Manifest 发布顺序

先发布并核验 QEMU 的测试提交，再在团队 manifest 增加一个 project，复用
`shaniu-agent` remote、路径 `external/qemu-bk7258`，revision 必须是那个完整 SHA。
本地尚未发布的 SHA 不能写入活动 manifest，以免 `repo sync` 获得不可解析的依赖。
不新增 linkfile，也不覆盖已有 prebuilts 或 sil-kit adapter。

有活动 pin 后，`qemu build` 可省略 `--source`，通过 `--workspace`（默认团队仓
父目录）找到源仓；它检查 HEAD 与 pin 一致且工作树干净。`--source` 始终是显式的
本地实验覆盖，构建结果记录实际 SHA 和 dirty 标志。

QEMU 当前上游拒绝 AI 生成贡献。本实现是明确标注的下游实验，不自动提交上游。
