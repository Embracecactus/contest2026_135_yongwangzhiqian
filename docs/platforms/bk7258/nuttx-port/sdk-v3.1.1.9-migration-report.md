# BK7258 SDK v3.1.1.9 迁移与板测报告（2026-07-31 历史快照）

日期：2026-07-31

> 本文记录当时从 legacy SDK 切换到 v3.1.1.9 的迁移证据。文中的 default、fallback、
> bundle 路径和构建命令均不是现行入口；legacy 不得作为当前 BK7258 信任、构建或下载
> 的候选与回退。现行 SDK 身份由 team manifest 负责，操作入口见
> [构建、烧录与调试 SOP](bk7258-build-flash-debug-sop.md)。

## 1. 结论

官方技术支持提供的 `bk_avdk_smp-release-v3.1.1.9` 已完成 CP/AP 编译、静态库导入、
ABI 比较、双镜像构建和真实 T5AI-Core 板冷启动验证。该轮迁移后默认链接 `v3.1.1.9`，
迁移前的 legacy SDK、旧 SDK 源码和 GitHub `vendor_beken` 参考仓均原样保留。

本次没有修改 `<openvela-workspace>/nuttx` 官方源码；所有实现均位于
contest repository 的 team overlay，受限 SDK bundle 仍不进入 Git。

## 2. 来源与保留策略

| 项目 | 路径 / 身份 | 处理 |
|---|---|---|
| 官方压缩包 | `<sdk-v3.1.1.9-source-archive>` | 保留 |
| WSL 压缩包路径 | `<sdk-v3.1.1.9-source-archive>` | 只读来源 |
| v3.1.1.9 source snapshot | `<sdk-v3.1.1.9-checkout>` | 新增、完整解压 |
| legacy source snapshot | `<legacy-sdk-checkout>` | 原样保留 |
| GitHub 参考仓 | `<vendor-beken-checkout>`，commit `85a08bf9761a873fb0f80e0b557f44de93005c65` | 原样保留 |
| legacy bundle | `bk_idk/armino_as_lib/versions/legacy/{cp,ap}` | 当时保留的比较样本 |
| v3.1.1.9 bundle | `bk_idk/armino_as_lib/versions/v3.1.1.9/{cp,ap}` | 当时默认 |

官方压缩包大小为 `207144722` bytes，SHA-256：

```text
39ae282d6d20f77734b7eed3ceb1c679427180d697b12ab3f61fcc39959efcbd
```

压缩包根目录 `README.md` / `README_CN.md` 均为空，因此没有把空 README 当作版本
说明；来源由文件名、archive 哈希和每个角色的 provenance 共同固定。

## 3. 编译与导入

使用：

```text
arm-none-eabi-gcc (15:10.3-2021.07-4) 10.3.1 20210621 (release)
```

官方 SDK CP/AP target：

```bash
make -C <sdk-v3.1.1.9-checkout> \
  bk7258_cp PROJECT=app COMPILER_TOOLCHAIN_PATH=/usr/bin -j8

make -C <sdk-v3.1.1.9-checkout> \
  bk7258_ap PROJECT=app COMPILER_TOOLCHAIN_PATH=/usr/bin -j8
```

导入脚本根据 `compile_commands.json` 重编 UART 对象并注入
`CONFIG_BK_PRINTF_DISABLE`，然后替换 `libdriver.a` 同名成员。两角色都验证该对象
不再引用 `bk_printf_init`。

| 角色 | manifest 条目 | manifest SHA-256 | final `libdriver.a` SHA-256 |
|---|---:|---|---|
| CP | 424 | `438c1bf16a37cbfe13adda7e7e99c5f757c82d7b6cc04d61521ca1836155c7be` | `e1b399eaef05878e3465a16cd9fdfcc0a340325bc46bc335e437096f1d17be88` |
| AP | 706 | `5d4b7908fd21201a5f5ec3537915209aaed0273dc9779d8ba72a40ab82056edc` | `da34c861c0b03424585616b79e25b26de1c9cc1e8f5bc0e9e7131ebd12f211d4` |

legacy 也补齐最终全量 manifest：

| 角色 | manifest 条目 | manifest SHA-256 |
|---|---:|---|
| CP | 428 | `9366420db842c327b6a1d772fc3c056299bdb2f2a7228082fdbf4d875387fbbf` |
| AP | 706 | `3fa5bd719d8d4e8c16ff535a8a25da9c2d86724ca5577c36a3bd1d4293fede82` |

## 4. legacy / latest 差异

两版本 CP/AP 的 `.a` archive 文件名集合完全一致：

- CP：81 个 archive；
- AP：101 个 archive。

CP latest 不再导出 legacy 中 4 个 loose `.obj`：`port.c.obj`、`portasm.c.obj`、
`rtos_init.c.obj`、`startup_bk7236.c.obj`。当前 NuttX 链接规则从未链接这些对象。

全文件比较：

| 角色 | legacy | latest | 内容相同 | 内容变化 | legacy only |
|---|---:|---:|---:|---:|---:|
| CP | 428 | 424 | 350 | 74 | 4 |
| AP | 706 | 706 | 638 | 68 | 0 |

变化头文件：

- CP：`bk_ble.h`、`pwr_clk.h`；
- AP：`app_time_intf.h`、`bk_ble.h`、`pwr_clk.h`、`wifi.h`、
  `wifi_types.h`。

全 archive 定义的 global symbol union 比较：

| 角色 | legacy | latest | latest 删除 | latest 新增 |
|---|---:|---:|---:|---:|
| CP | 12626 | 12622 | 4 | 0 |
| AP | 13713 | 13706 | 7 | 0 |

CP 删除符号：

```text
app_sec_reject_pairing
bk_ble_reject_pairing
ble_dut_stop
cli_flash_perf_cmd
```

AP 删除符号：

```text
app_sec_reject_pairing
bk_ble_reject_pairing
ble_dut_start
ble_dut_stop
cli_flash_perf_cmd
mb_flash_op_finish
mb_flash_op_prepare
```

它们属于未使用的 Bluetooth/CLI 路径，或 AP `libdriver` 的 flash mailbox helper；
当前 CP/AP NuttX 实际链接均不需要这些符号。

## 5. 双版本构建比较

两个版本都完整执行 CP → AP → CP restore，并通过 manifest 与角色路径隔离检查。
保存的构建目录：

```text
<openvela-workspace>/nuttx/bk7258-dual-sdk-legacy
<openvela-workspace>/nuttx/bk7258-dual-sdk-v3.1.1.9
```

raw image：

| 产物 | legacy SHA-256 | v3.1.1.9 SHA-256 |
|---|---|---|
| CP `app.bin` | `6549ae0e706b18b67d1cac625e60426cbabedab3a89aeae2581d7b9b92b9038a` | `3b0585dd93fd6f270c146c131ca782c1d152dbac7f0f73570cabf73294949e72` |
| AP `app1.bin` | `46e513798d5248f43bbe8227684a03a4109b126903b7c934c9faa9ef6a327d65` | `46e513798d5248f43bbe8227684a03a4109b126903b7c934c9faa9ef6a327d65` |
| factory | `dbe5e4e17afe4fbd75c80f438602b332e1cf94a635ca3bd75e14dfe33cf470e0` | `6edaa4dffc44ba808bb656628aaaba7d42283a8c72c94cbdb6dfd5bbad6cabca` |

ELF size 完全一致：

| 角色 | text | data | bss | total |
|---|---:|---:|---:|---:|
| CP legacy/latest | 172940 | 5924 | 10316 | 189180 |
| AP legacy/latest | 56836 | 700 | 7484 | 65020 |

CP/AP 的全部 symbol address 也完全一致。AP raw image 逐字节一致；CP raw image 只在
3 个字节上不同，位于 `g_version` 的编译时间字符串，不是 SDK 代码或运行时布局差异。
因此本次实际 NuttX 固件在可执行语义上等价，但 build provenance 明确区分两个 bundle。

## 6. 实板验证

烧录 latest factory：

```text
<openvela-workspace>/nuttx/
  bk7258-dual-sdk-v3.1.1.9/all-app-factory.bin
```

烧录日志：

```text
<openvela-workspace>/logs/bk7258-auto-debug/20260731-140133
```

BK loader 完成 erase/write/reboot；warm capture 为 `PASS_NSH`。随后使用 RTS 执行三轮
物理 RESET，均为 `PASS_NSH` 且 `cold_path=yes`：

```text
<openvela-workspace>/logs/bk7258-auto-debug/20260731-140223
<openvela-workspace>/logs/bk7258-auto-debug/20260731-140257
<openvela-workspace>/logs/bk7258-auto-debug/20260731-140327
```

每轮均出现：

```text
u_bootloader enter
BClk A5=8407A76C A9=787BC8A4
partition app @ 0x02010000
jump to:0x02010000
JMP
[hal]
NuttShell (NSH)
nsh>
```

`create_socket failed.` 仍是已知非致命输出，与迁移前一致。

## 7. 历史边界

该轮曾验证 v3.1.1.9 默认 bundle 与 legacy 切换，但相关环境变量、目录和双镜像脚本均已
退出公共接口。现行流程不得从本文恢复 legacy 回退路径，也不得把历史 bundle 当作当前
SDK 身份；SDK 版本、来源与安装状态以 team manifest 和统一 CLI 的 `sdk` 域为准。

这次迁移只验证了当时的 SDK 静态库来源切换，不等同于现行启动链或信任链验收。
