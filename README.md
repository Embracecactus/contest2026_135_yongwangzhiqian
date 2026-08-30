# BK7258 三核 openvela 适配

[English](README_EN.md) | 简体中文

## 一、作品简介

本作品为 Beken BK7258（三核 Arm Cortex-M33）提供完整的 openvela/NuttX
平台适配，并在 T5-Board、T5AI-Core 和 AIToyBoard（工程标识 `aidk_ai_toy`，
既有文档名 AIDK AI Toy）三块物理板之间复用同一套
SoC 实现。系统不是官方模板假定的单镜像模型，而是由 CPU0 上的 CP NuttX 与
CPU1/CPU2 上的 AP SMP NuttX 组成配对系统。

主要交付包括：

- CP/AP/CPU2 启动、80 槽向量表、SDK IRQ bridge、UART/NSH、定时器、堆与板级
  bring-up；
- RPMsg/RPTUN、Wi-Fi/Bluetooth、PSRAM、音视频及常用外设的 SDK wrapper；
- 项目自有 BL1、NuttX MCUboot BL2、同槽签名 CP/AP 和回滚计数约束；
- 统一的 CMake 构建、分区生成、打包、校验和主机回归入口；
- 可追溯的源码、构建、实板串口和 AI Coding 证据。

功能是否完成必须以当前配置和对应实板记录为边界，不能把一块板或历史 profile 的
结果推广到所有板型。三板维护配置见 [板级配置说明](boards/bk7258/CONFIGS.md)，
对应验收记录见 [`docs/verification/bk7258/`](docs/verification/bk7258/)；完整技术报告见
[移植报告](docs/platforms/bk7258/porting-report.md)，官方清单的逐条口径见
[符合性复核说明](docs/platforms/bk7258/official-compliance-review.md)。

## 二、选题方向

**新硬件适配。** 作品重点是把 BK7258 的三核启动、芯片驱动、板级配置、Beken SDK
和安全启动链接入 openvela，而不是在已有 BSP 上增加一个应用。三核与双镜像是本
平台的真实架构约束，相关偏离均在符合性复核说明中显式记录。

## 三、目录结构

| 路径 | 内容 |
|---|---|
| `chips/bk7258/` | CP/AP/CPU2、IRQ、定时器、外设 wrapper、BL1/BL2 与芯片 Kconfig |
| `boards/bk7258/` | 三块物理板、CP/AP 配对配置、分区 CSV、公共链接脚本和 bring-up |
| `tools/bk7258/` | 唯一维护入口：工具链、SDK bundle、构建、签名、打包、部署和校验 |
| `tests/host/bk7258/` | 直接编译现役源码的主机回归；不映射进 OpenVela 应用树 |
| `app/testing/bk7258/` | 三块 BK7258 板共用的官方格式 CMocka 板上应用 |
| `tests/pytest/test_bk7258/` | 链入官方 pytest 串口框架的三板实板验收 |
| `docs/platforms/bk7258/` | 移植报告、符合性说明、调试方法和历史阶段记录 |
| `docs/verification/bk7258/` | 带构建身份和适用边界的不可变验收记录 |
| `logs/lijian/` | 按大赛格式导出的 AI Coding JSONL 日志 |
| `logs/bk7258-*` | 早期硬件原始证据；不是 AI 对话日志 |
| `prebuilt/` | 本机安装的锁定工具链；二进制内容为可再生成的忽略文件 |
| `chips/bk7258/bk_idk/armino_as_lib/` | 从 manifest 锁定 SDK 重建的本机 bundle；不分发第三方二进制 |

Manifest 将团队维护的 chip、board、工具、应用和目标端测试映射到 openvela 工作区的
标准扩展位置。Host 测试、`docs/` 和 `logs/` 只存在于团队仓；目标端
CMocka 链接到官方 `apps/testing/bk7258` 自动发现点，串口用例只链接到官方 pytest 的测试子目录。

## 四、运行方式

### 1. 获取完整工作区

以下命令显式选中默认项目和 BK7258 SDK 组。SDK 项目没有 `notdefault` 标记，因此
普通默认同步也会包含它；显式写出分组是为了让复现输入一目了然。

```bash
repo init -u https://github.com/open-vela/contest2026_135_yongwangzhiqian \
  -b dev-ai-contest-2026 \
  -m contest2026_135_yongwangzhiqian.xml \
  -g default,bk7258-sdk
repo sync -c -j8
cd contest2026_135_yongwangzhiqian
```

建议使用 Ubuntu 22.04，并预先安装 openvela 常规构建依赖、Python 3、CMake、Ninja
和 GNU Make。Arm 编译器不从系统 `PATH` 选择。

### 2. 安装锁定工具链

工具会从 `tools/bk7258/toolchain.json` 指定的 Arm 官方 HTTPS 地址下载归档，校验
SHA-256 后安装到被忽略的 `prebuilt/` 目录。也可用 `--archive` 指定已下载的同一
归档。

```bash
tools/bk7258/bk7258.py toolchain install
tools/bk7258/bk7258.py toolchain verify
```

### 3. 重建 SDK bundle

SDK 源码由 manifest 固定在 `vendor/beken/bk_avdk_smp`。T5-Board 与 T5AI-Core 使用
`cp` + `ap`；AIDK AI Toy 使用 `cp-aidk` + `ap`。

```bash
tools/bk7258/bk7258.py sdk rebuild \
  --profile cp --source ../vendor/beken/bk_avdk_smp --jobs 8
tools/bk7258/bk7258.py sdk rebuild \
  --profile ap --source ../vendor/beken/bk_avdk_smp --jobs 8
tools/bk7258/bk7258.py sdk verify --profile cp
tools/bk7258/bk7258.py sdk verify --profile ap
```

构建 AIDK AI Toy 前，将上面的 `cp` profile 改为 `cp-aidk`。已取得与跟踪哈希一致的
预制 bundle 时，也可使用 `sdk install --profile <name> --bundle <path>`；bundle
仍须通过 `sdk verify`。

### 4. 构建 CP/AP 配对系统

下面的 direct 模式用于无签名 bring-up 和复现检查：

```bash
tools/bk7258/bk7258.py build \
  --board t5ai_core --boot direct --jobs 8
```

也可将板名改为 `t5_board` 或 `aidk_ai_toy`。入口会解析板级 `openvela.conf`，生成
CP/AP 私有构建配置和分区头/链接输入，然后分别调用官方 `build.sh ... --cmake`。
`BK7258_SDK_DIR`、工具链和分区变量是 wrapper 的受校验内部契约，用户无需手工设置。

构建结束会打印 build manifest、CP/AP ELF/原始 bin 和最终 Flash 段的精确路径与
SHA-256。多镜像系统的产物按分区角色命名，例如 `boot.bin`、`cp.bin`、`ap.bin`、
`pair.bin` 和签名发布中的 `bl2-a.bin`；单镜像示例名 `vela_ap.bin` 不适用于此布局。
这些分区产物不能单独作为整机下载交付。direct 调试包必须继续通过唯一入口生成并
验证完整 ZIP。`$DEVICE_BASE` 是从同一块目标板读取的完整 Flash，不能使用另一块板
的 dump：

```bash
tools/bk7258/bk7258.py package accept-base \
  --board "$BOARD" --base "$DEVICE_BASE" \
  --device-id "$DEVICE_ID" --capture-method fixture-readback \
  --output "$DEVICE_BASE_EVIDENCE"
tools/bk7258/bk7258.py package delivery \
  --build-manifest "$DIRECT_MANIFEST" --unsigned \
  --version 0.1.0+1 \
  --base "$DEVICE_BASE" --base-evidence "$DEVICE_BASE_EVIDENCE" \
  --output "$BOARD-direct-diagnostic.zip"
tools/bk7258/bk7258.py verify delivery \
  --delivery "$BOARD-direct-diagnostic.zip"
```

`accept-base` 会把完整读回绑定到板型、布局、稳定设备 ID 和采集方式；它防止误把
同容量的另一板读回混入产品包，但操作者/受控夹具仍负责证明读回确实来自该设备。
ZIP 同时包含 accepted-base/release/build manifest、板级发布策略、SHA-256、烧录说明和底层可
验证包；`recovery/*-full-flash.bin` 的长度由板级分区 CSV 推导，当前三板均为完整
8 MiB。它保留同一设备的配置、持久数据和 MAC/RF/网络校准状态，因此是设备绑定的
恢复包，不能复制到另一块板。unsigned direct 仍只用于 bring-up/诊断，不能冒充
正式签名发布。未接入量产写号与校准流程前，清单会明确标记工厂镜像
`requires-provisioning`，不会伪造一个通用 factory BIN。

`--boot mcuboot` 是正式签名链，但需要该次发布新生成的 BL1/MCUboot 公钥、私钥侧
发布步骤和严格递增的回滚计数。不要复用历史私钥。完整流程和烧录边界见
[构建/烧录/调试 SOP](docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md)。
正式版本分别通过 `release full` 与 `release ota` 生成并验证，再使用
`release product --full-release ... --base ... --ota-release ...
--ota-required-source-version ...` 合成每块板、每个版本独立的产品 ZIP。OTA 只更新 CP/AP，
并在清单中绑定来源版本和来源设备当前必须信任的 MCUboot 根；有线恢复另行记录它
安装的新根，密钥轮换时两者允许不同。

### 5. 主机回归

安装 `cmocka` 开发包后，从团队仓根目录执行：

```bash
make -C tests/host/bk7258 check
```

成功标志为 `BK7258_HOST_TEST_PASS`。该结果只覆盖 mock 环境中的逻辑与 ABI；硬件
能力必须引用对应的实板记录。

### 6. 官方目标端与串口测试

三块板各自的 `configs/xts` CP 配置与同板 `openvela_ap` 配对，启用同一个
`cmocka_bk7258_board_test`。下载后可从 CP NuttShell 直接运行，或从工作区
`tests/scripts` 使用官方 pytest，并把 `-B` 设为 `t5_board`、`t5ai_core` 或
`aidk_ai_toy`。具体参数见
[`tests/pytest/test_bk7258/README.md`](tests/pytest/test_bk7258/README.md)。

## 五、AI Coding 使用说明

AI 参与了需求拆解、官方/SDK 源码交叉核对、启动与中断根因分析、实现和测试生成、
实板日志解释、威胁建模以及文档维护。关键做法是把 AI 结论当作待验证假设：源码
所有权、分区、符号、构建产物和硬件结果都必须由可重复命令或原始证据确认。

符合大赛格式的对话日志位于 `logs/lijian/<date>/<tool>__<sid>.jsonl`，会话索引见
`logs/lijian/manifest.json`。`logs/bk7258-*` 是早期串口/安全启动原始证据，不属于
AI 日志；新的结构化实板结论统一写入 `docs/verification/bk7258/`。

## 许可证

除文件或目录另有声明外，本仓库原创内容按 Apache License 2.0 授权，许可证全文见
[`LICENSE`](LICENSE)。第三方及上游派生材料继续适用其原有版权和许可证声明；manifest
引用但未存储在本仓库中的项目由各自许可证管理。BK7258 主机测试的逐类来源说明见
[`tests/host/bk7258/PROVENANCE.md`](tests/host/bk7258/PROVENANCE.md)，全仓源码分类与许可证审计见
[`SOURCE_PROVENANCE.md`](SOURCE_PROVENANCE.md)。

## 评审入口

- [符合性复核说明（中文）](docs/platforms/bk7258/official-compliance-review.md) /
  [English](docs/platforms/bk7258/official-compliance-review.en.md)
- [openvela 文档适配矩阵](docs/platforms/bk7258/openvela-document-adaptation-matrix.md)
- [BK7258 板级配置与架构](boards/bk7258/README.md)
- [BK7258 主机测试说明](tests/host/bk7258/README.md)
- [BK7258 OpenVela 板上测试](app/testing/bk7258/README.md)
- [AI Coding 日志格式](logs/README.md)
