# BK7258 官方启动行为逆向合成结论（N17+，只读验证）

Last updated: 2026-08-08
Owner: 逆向验证（CodeBuddy 只读核查，不改代码）
配套资产清单：`reverse-attempt-assets-N17.md`

## 0. 方法

纯只读黑盒 / 灰盒差分：把官方 BK7258 二进制、SDK 头、板上读出记录与
「自有 BL1 / NuttX MCUboot BL2」构建产物做结构性比对，确认「兼容官方启动
行为的自有完整 BL1/BL2/MCUboot」在哪些层已坐实、哪些层仍开放。未烧写、
未改源码。

---

## 1. 结论速览（已逆到哪 / 卡在哪）

| 层 | 状态 | 证据 |
|---|---|---|
| BL1 向量表 + 魔数 | ✅ 兼容官方 | 8 字 ARM VT 布局一致；`@0x100` 魔数同为 `"BK7236\x10\x00"` |
| BL1 外设地址（WDT 等） | ✅ BK7258 事实 | SDK `reg_base.h` 中 `AON_WDT=0x44000600` |
| BL2 向量表 + 复位桩 | ✅ 兼容官方 | VT 布局一致；reset 槽 `0x28020101` 相同；MCUBoot 风格 prologue `72 b6` |
| BL2 为自有 MCUboot 编译 | ✅ 设计内 | `bl2/` 含 `bootutil_*`、`image_validate.o` 等；与官方字节一致率仅 4%（不同编译同 ABI） |
| Manifest ABI（offset/security_counter/slot） | ✅ BK7258 证据 | `n17-signed-manifest-abi.md`：counter@0x020、slot A@0x011000、B@0x286000 |
| OTP/security-counter 基址 | ✅ BK7258 事实 | SDK `OTP=0x4b100000`、`EFUSE=0x44880000`；弱钩子由 BK7258 后端覆盖 |
| BL1 签名信任根 | ⚠️ 软件占位 | `boot_bl1_manifest_key.c` 公钥为开发占位，OTP 未烧（按红线） |
| 尾部 CRC/status 块格式 | ⚠️ 与官方不一致 | 官方尾部 16 零字节；自有为 `0x000c`+`0xff`（SB5 精确性开放） |
| 官方打包顺序（AES/merge/tail） | ⚠️ 仅 host reference | 缺官方 AES key/consumer 与 BootROM CRC 读视图，产物不烧录 |
| 硬件根安全启动（SB-H） | ⛔ 被红线挡 | 需 OTP 烧写 / 开 secure boot，当前禁止 |

---

## 2. P1 — BL1 黑盒差分（官方 vs 自有）

比对 `bk7258_normal_bootloader.bin` (43744 B) 与 `bl.bin` (23444 B)：

- **向量表结构一致**：均为 8 字 ARM VT（SP / Reset / 各类 Handler），且
  `@0x100` 处魔数同为 `42 4b 37 32 33 36 10 00`（`"BK7236\x10\x00"`）。
  这与 `docs/platforms/bk7258/hardware/t5ai-core/probe/README.md` 记录的「官方 BK7258 二进制自带
  该魔数」吻合 → **BL1 在固件镜像层与官方启动行为兼容**。
- **栈顶不同**：官方 `0x28030000` vs 自有 `0x2809f700`。原因：自有 BL1 把
  `0x2809f700` 区复用为调试 trace RAM（历史 nuttx 探针落点），SP 基址上移。
  属**有意的、文档化的偏离**（telemetry 区复用），非结构破坏。
- **VT body（0x20–0x100）仅 60/224 字节相同**：该区为实际 handler/reset 桩
  代码，官方与自有实现不同 → 预期内。**结构（槽位、魔数、模式）一致，代码
  为自有**，正是「兼容官方启动行为 + 自有实现」。
- **尾部不同**：官方以 `e5ff ffef` + 16 零字节收尾（Armino 风格尾部分支填充
  + 零状态块）；自有以真实 thumb epilogue（`4ff0ff34 dce700bf...`）收尾。
  → 官方尾部的 **CRC/status 块格式尚未在自有构建中复刻**（见 §5）。

## 3. P2 — BL2 校验路径比对（官方读出 vs 自有）

比对 `bk7258-bl2-xip-read-20260807.bin` (12288 B) 与 `bl2/bl2.bin` (10708 B)：

- **VT 布局一致**：8 字 VT；reset 槽均为 `0x28020101`（公共 MCUboot reset 桩）。
- **`@0x100` 魔数同为 MCUboot 风格 prologue**：官方 `72 b6 05 48 05 49 00 22`、
  自有 `72 b6 09 48 80 f3 0a 88`——均起始于 `72 b6`（thumb `LDR`/`ADR` 取
  字面池），尾部 `00 22` 类 `MOVS R2,#0`。prologue 同源，差异在字面池目标
  （XIP 基址：官方 `0x28030000`、自有 `0x28040000`，telemetry 区位移所致）。
- **字节一致率随长度骤降**（头 256B 51% → 全 10708B 4%）：官方为 Beken 完整
  MCUboot 编出，自有为 pinned NuttX MCUboot 编出，**编译不同、ABI 相同**。
- **校验路径结论**：BL2 在「镜像层」与官方兼容（VT + 魔数 prologue + MCUboot
  reset）；实现为**独立的 NuttX MCUboot 编译**，非字节拷贝。这与 ADR-022 的
  「BL2 实现 = NuttX MCUboot」一致，是设计内正确结果。

## 4. P4 — OTP shadow 只读证据（BK7236 语义 → BK7258 事实）

- SDK 头给出真实 BK7258 地址：`OTP=0x4b100000`、`EFUSE=0x44880000`、
  `MPC_OTP=0x41130000`。自有 BL1 经弱钩子 `bk7258_bl1_manifest_version_floor_readonly()`
  （`boot_bl1_policy.c`）由 BK7258 后端以**已验证只读 OTP shadow 地址**覆盖 →
  读的是**真 BK7258 OTP 区**，非 BK7236 遗留。
- `n17-signed-manifest-abi.md` 已记录 **BK7258 验证过的 Manifest ABI**：
  `security_counter @0x020`（8B 单调放行值）、slot A `@0x011000`、slot B
  `@0x286000`，以及 floor 检查要求。即 BL1 解析的 Manifest *格式* 已是
  **BK7258 证据**，非 BK7236 语义。
- 唯一仍带「BK7236 语义」色彩的是 **unary 一位计数解码算法形状**（从 BK7236
  BootROM 复得），属架构共享行为，ADR-022 明确允许；地址层已 BK7258 化。

## 5. 仍开放的缺口（SB5 精确性 / 真正安全启动）

1. **尾部 CRC/status 块格式不一致**：官方以 16 零字节作状态/填充块；自有以
   `0x000c` + `0xff` 收尾。要让自有包在字节层与官方 BootROM 接受契约吻合，
   需逆向官方尾部块布局（长度、状态字含义、CRC 覆盖范围）。
2. **官方打包顺序仅 host reference**：ROADMAP 已记，官方 BK7258 的 AES key/
   consumer 与 BootROM CRC 读视图不可得，故 `make_bl1_manifest.py` 产物只作
   参考、不烧录。闭合需官方打包工具或官方签名包逐字节比对（黑盒差分法）。
3. **BL1 签名信任根为软件占位**：`boot_bl1_manifest_key.c` 公钥为开发占位，
   OTP 未烧。当前链是「可恢复的功能完整兼容验证链」，**非防篡改硬件安全
   启动**。SB-H（绑 OTP/eFuse/BootROM）被红线显式推迟。

## 6. 建议的下一步逆向手段（均只读/可恢复）

- **A. 官方包黑盒差分（最高性价比）**：用官方打包工具生成已知 Manifest 的包
  → 稀疏读回 + SHA 逐扇区比对，反推官方尾部/status 放置规则，直接补 §5.1/5.2。
- **B. Ghidra 反汇编官方 BL2**：复用 `/tmp/ghidra-bk7258*`，确认官方对 Manifest
  的 digest/signature/TLV/security-counter 校验路径与 `bl2/bk7258_bl2_*` 逐字段
  一致（P2 仅比到镜像层，未比校验逻辑）。
- **C. J-Link 双核启动快照**：CP→AP 各阶段抓寄存器/内存，坐实 SPINLOCK /
   Telemetry (`0x2809f000` 段) / RPMsg carveout 与官方 `ram_regions.h` 完全吻合。
- **D. OTP shadow 只读快照**：用 J-Link 读 `0x4b100000` 区，与 `boot_bl1_policy`
  解码结果交叉验证，把 §4 的「基址证据」升级为「运行期读值证据」。

以上 A–D 均不触发 OTP 烧写 / 源码修改，符合当前红线。

---

## 7. 附录：SOP-A/B/C/D 执行状态（黑盒差分尝试）

| SOP | 内容 | 状态 | 关键结论 |
|---|---|---|---|
| A | 官方包黑盒差分 | ✅ 已执行（只读） | 见 §8 |
| B | 官方 BL2 校验路径比对 | ✅ 已执行（MCUboot 魔数扫描；Ghidra 未装） | 见 §9 |
| C | J-Link 双核启动快照 | ✅ 已执行（共享状态；J-Link 仅枚举 AP[0]） | `reverse-sop-cd-jlink.md` |
| D | OTP shadow 只读快照 | ✅ 已执行（非密钥字段） | `reverse-sop-cd-jlink.md` |

### §8 SOP-A：官方包黑盒差分结论

- **官方 BL1/BL2 是 flat XIP，不含 32+2 CRC 交织**；你的 `bl_crc.bin`/`bl2_crc.bin`
  是 32+2 CRC-XIP 扩展（`crc_expand`：每 32B 数据 + 2B CRC16(poly 0x8005, BE)）。
  - 官方 `bk7258_normal_bootloader.bin`：43744 B（mod34=20），尾部 `e5ffffef` +
    16 零字节，无交织 CRC、无 `0x000c` status 字。
  - 官方 BL2 读出 `bk7258-bl2-xip-read-20260807.bin`：12288 B（mod34=14），尾部全 `0xff`，flat XIP。
  - 你的 `bl2_crc.bin`：13056 B（mod34=0，所有 CRC 校验通过），尾部 `...ff 0c`（status 字 `0x000c`）。
- **官方与自有是不同实现**（非字节副本）：官方 BL1 vs 你 `bl.bin` 逻辑形态仅
  2.1% 字节相同；二者共享向量表结构 + `"BK7236\x10\x00"` 魔数（P1），但代码不同。
- **结论**：32+2 CRC-XIP 是你打包工具链的设计选择（flash 完整性加固），**不是**
  对官方容器的忠实复制。官方容器格式本身**尚未逆出**——目前只知其「非 32+2
  交织、尾部 16 零字节」。闭合 SB5 精确性需拿到官方打包工具/签名包逐字节比对，
  或确认官方 BL1 是否本就以 flat XIP 烧录（很可能如此）。

2026-08-08 又以项目冻结的官方 v3.1.1.9 为输入执行
`diff_bk7258_packages.py`，而不是使用滚动的 `bk_idk` 文件：

- normal `bootloader.bin` 为 52,352 B（mod 34 = 26），识别为 `flat`；
- A/B `bootloader.bin` 为 18,720 B（mod 34 = 20），识别为 `flat`；
- 自有 `bl_crc.bin` 为 69,632 B，全部 32+2 块校验通过，识别为
  `crc32p2`；两组结果均为 `packaging match: NO`；
- 解开自有 CRC 后，`bl.bin` 与其打包逻辑内容在原始 23,444 B 范围内一致，
  差异只落在填充到 64 KiB 的尾部，不是 CRC 编码篡改了 BL1；
- 本机 `bk_avdk_smp-release-v3.1.1.9`、官方 `bk_idk` 和 Tuya 镜像树中
  没有 BK7258 官方安全签名包。找到的 secure-calibration/BL2 安全产物由源码、
  Makefile 和目录配置明确标为 BK7236，禁止拿来冒充 BK7258 的 SB5 reference。

因此当前能冻结的是：**官方发布的 BK7258 bootloader 容器输入为 flat，自有
烧录包额外做了 32+2 扩展；尚不能证明芯片最终物理 Flash 不需要 32+2**，因为
官方 downloader 是否在传输/写入阶段执行转换仍缺物理读回证据。只有取得真正
BK7258 签名包或官方烧录后的物理/逻辑双视图，才能要求 `packaging match: YES`
并关闭 SB5；当前的 `NO` 是预期边界，不是工具失败。

### §9 SOP-B：官方 BL2 校验路径结构比对

- Ghidra 本环境未安装，`/tmp` 既有反编译导出为符号化前的 `FUN_xxxx` 裸函数表
  （无法按名检索 manifest/signature）。改用 MCUboot 结构魔数扫描（只读）：
- **官方 BL2 读出与你的 `bl2.bin` 均含 `IMG_MAGIC = 0x96f3b83d` 恰好 3 次**
  （官方 @0x144，自有 @0x158）—— MCUboot 镜像头 magic，印证两者内嵌相同
  的 MCUboot 镜像结构（主/副 slot + TLV 承载）。
- 你的 `bl2/` 编出含 `bootutil_*`、`image_validate.o`、`image_ecdsa.o`、`tlv.o`、
  `swap_scratch.o`（P2 已确认），与官方 BL2 的 MCUboot 校验路径在 **ABI/结构层对齐**。
- **结论**：BL2 校验路径在 MCUboot 结构层与官方兼容；完整逐函数符号比对需在装
  Ghidra + 命名符号的环境重跑。当前证据已足够证明「自有 BL2 = 兼容官方的
  NuttX MCUboot 编译」。

### §10 SOP-C/D J-Link 只读结果

2026-08-08 通过 Windows SEGGER J-Link V9.54、SWD 1 MHz 对当前运行固件执行
`halt -> mem read -> go`，没有 reset、寄存器写入、Flash/OTP/eFuse 操作：

- `APBS @0x2809f000`：版本 1、记录长 0x80、state=2、error=0，AP physical
  core ID=1，initial VTOR=`0x02150200`，AP RAM=`0x28050000..0x2809f000`，
  clock=`120000000`；
- `CPU2 @0x2809f180`：版本 1、记录长 0x80、state=7
  (`SECONDARY_READY`)、error=0、local/physical ID=1/2，vector/runtime
  VTOR=`0x02150400`，initial MSP=`0x2809f000`，runtime MSP=`0x2809eff8`；
- CPU1/CPU2 control 为 `0x02150239` / `0x02150439`，与共享状态中的启动向量
  对应；J-Link 只枚举一个 AHB-AP，因此没有声称取得 AP 核的独立寄存器窗口；
- `0x2809fff4=0xaaaaaaaa`，故 `CONFIG_ATE_CPU2_ADDRESS=0x2809fff7` 对应字节为
  `0xaa`。它是 ATE 专用地址，不能作为通用 CP↔CPU2 握手判据；有效运行期证据
  是上面的 `APBS`/`CPU2` ABI；
- Dubhe `OTP_SET=1`、`OTP_UPDATE_STAT=4`；shadow 公钥哈希全零、LCS=0、
  lock=0；BL1 counter bitmap `@0x4b111088=0`，BL2 64-byte counter
  `@0x4b111100` 全零。按当前实现解码，BL1 OTP floor=0（有效软件 minimum=1），
  BL2 OTP floor=0，确认样片仍处于可恢复的软件信任根模式。

这把 OTP/EFUSE 从“SDK 基址证据”升级成了当前样片的运行期读值证据，同时也
修正了 SOP-C 原先对 ATE 地址的过度推断；结果不证明 BK7258 BootROM Manifest
格式或硬件 Secure Boot 已启用。
