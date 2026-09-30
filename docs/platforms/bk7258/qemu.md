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

fixture 验证 UART、SRAM 四别名、内部及外部参考 SysTick 中断、真实看门狗 NMI、CPU1/CPU2 释放、
每核 TCM、halt/resume、reset、UART RX IRQ、三核独立 mailbox IRQ63 往返与错核保护，
及完整系统重启。输入 `X` 必须进入
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
BK7258_QEMU_SOURCE=/path/to/qemu-bk7258 \
BK7258_QEMU_BUILD=/path/to/out/qemu \
  python3 tests/host/bk7258/test_bk7258_qemu.py -v
```

## 受限原生 NuttX/NSH 验收

`aidk_ai_toy/native_nsh` 的用途与负向配置契约以
[配置目录](../../../boards/bk7258/CONFIGS.md) 为准。它用现有 Kconfig 裁剪可选产品
外围，仍运行原生 NuttX/SDK 启动、UART、NVIC 和 SysTick，不依赖 QEMU 检测分支。
它证明受限 CP 内核/控制台，不能代替未改产品 CP、AP SMP 或完整产品验收。

先按现有构建 SOP 准备 manifest 固定的固件依赖、GCC 10.3-2021.10 和 cp-aidk /
ap-aidk SDK；不要拿裸机 fixture 的任意编译器构建 NuttX。随后运行：

```sh
python3 tools/bk7258/bk7258.py build \
  --cp-config boards/bk7258/aidk_ai_toy/configs/native_nsh \
  --ap-config boards/bk7258/aidk_ai_toy/configs/openvela_ap \
  --partition boards/bk7258/common/partitions/bk7258/bk7258_ab_fixed_block_full_release.csv \
  --boot direct --jobs 6
python3 tools/bk7258/bk7258.py qemu native-nsh \
  --qemu /path/to/out/qemu/qemu-system-arm \
  --build-manifest /path/to/printed/direct/build-manifest.json \
  --output /path/to/out/native-nsh-evidence
```

入口复用 build manifest 的路径、角色和哈希校验，只接受 `direct/native_nsh` CP；
只加载 CP ELF，普通 AP 的构建结果不等于启动证据。此步骤不签名、不执行 ROM /
BL1 / BL2，也不烧板。

自动验收覆盖原生 NSH 标识、任务列表、短 UART RX 输入、后台 sleep 与前台进展、
SysTick 驱动的 uptime 增长、非法命令后继续工作、固件 reboot 及重启后的命令。
输出保留 UART、MMIO、失败原因、ELF/模拟器/构建 manifest 哈希和完整构建来源。
超时、异常退出、证据不符即失败，不把收到一次提示符当完整通过。

上游 NuttX 的 Cortex-M33 强制选择 `ARCH_HAVE_DEBUG`，初始化会探测 QEMU 尚未实现的
FPB/DWT/debug-monitor。验收只允许每次启动准确的 8 条探测日志，完整保留并计数，
其余未知访问全部失败；DWT 周期计数读取不在允许列表。该例外不证明硬件调试、
断点、性能计数或真实时间精度。

宿主模型回归另覆盖 UART idle 延迟/屏蔽/W1C/取消、GPIO 已挂起中断屏蔽、PMU
提交键与保持、模拟寄存器异步事务/忙拒绝/重启取消、两类看门狗和时钟门控。
设置上面的 SOURCE/BUILD 环境变量还会编译真实 SysTick/ptimer 源码，执行 15 个
定向测试与上游现有 576 个 ptimer 测试；未设置时明确 skip，不能当已运行。

未改产品 CP 已越过 CKMN 和 MBOX0 初始化，最早缺失设备目前为 Flash 控制器
`0x44030008`。回归通过表示仍准确停在已知缺口，绝不表示产品启动成功。用正常产品构建生成的 ELF 可复测：

```sh
BK7258_QEMU=/path/to/qemu-system-arm \
BK7258_PRODUCT_CP_ELF=/path/to/product/cp/cmake/nuttx \
BK7258_NM=/path/to/locked/arm-none-eabi-nm \
BK7258_QEMU_EVIDENCE=/path/to/product-stop-evidence \
  python3 tests/host/bk7258/test_bk7258_qemu.py ProductCPStop -v
```

CKMN 已按真实时钟比计数并验证失钟/取消，MBOX0 v2 已用真实八槽队列和每核 IRQ63
验证顺序、满队列、保护及原生三核往返。其未知边界以模型文档的显式策略为准，
不代表完整时钟校准或 RPMsg 软件验收。当前还缺 Flash 控制器/JEDEC/持久化、PSRAM、
legacy MBOX1、RPMsg、真实 AP SMP 和 boot/OTA；其支持状态由模型文档统一定义，不凭 NSH 或三个 machine 名称推断。

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
