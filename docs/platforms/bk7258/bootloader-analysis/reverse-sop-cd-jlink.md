# 逆向 SOP-C / SOP-D：J-Link 只读快照与 OTP shadow 交叉验证

Last updated: 2026-08-08
Owner: 逆向验证（CodeBuddy 只读核查）
状态：**2026-08-08 已在当前 BK7258 样片执行**（结果见本文 C.4/D.4；命令模板
继续保留供复查）

红线：仅只读 `mem read`，**不 halt 烧写、不改 OTP/eFuse、不开 secure boot**。
J-Link 连接本身是可恢复的（你之前经验：J-Link 软复位对该板无效，需物理复位；
只读 dump 不触发复位风险，但抓快照前先确认板在已知启动阶段）。

---

## SOP-C：CP→AP 双核启动寄存器/内存快照

目的：坐实双核交接 ABI 与官方 `ram_regions.h` / `sdkconfig` 完全一致，
闭合 ADR-022「BK7236 语义 → BK7258 事实」的最后一块（运行期值）。

### C.0 官方参考值（已从 SDK 提取，无需再逆）
- SRAM 基址（bk7258/reg_base.h）：SRAM0 `0x28000000`(64K) / SRAM1 `0x28010000`(64K)
  / SRAM2 `0x28020000`(128K) / SRAM3 `0x28040000`(128K) / SRAM4 `0x28060000`(128K)
  / SRAM5 `0x28080000`(128K)
- SPINLOCK：`CONFIG_SPINLOCK_SECTION=1`，dynamic 512 + static 128
  （bk7258/sdkconfig.h:283-286）
- CP↔CPU2 IPC 地址：`CONFIG_ATE_CPU2_ADDRESS = 0x2809FFF7`
  （落在 `0x2809f000` telemetry 区 —— 即你 BL1 复用的 trace RAM 区）
- 已知工作区（verify_bk7258_ota_boot.py 断言）：
  `g_boot_ota_scratch @ 0x2800D000/0x2000`、`g_boot_ota_metadata @ 0x2800F000/0x1000`、
  `.boot_ota_ramfunc @ 0x2800C000`

### C.1 抓拍点（每个点 halt→read→resume，单核）
| 阶段 | CP(CPU0) 关注 | AP(CPU1/CPU2) 关注 |
|---|---|---|
| 上电→BL1 入口 | SP/PC、SOC_AON_WDT `0x44000600` 初值 | 观察 AP 是否仍 in reset（`0x2809f700` trace 区） |
| BL1→BL2 交接前 | `r9`=复位向量、`r0-r8/r10-r12` 清零、MSP | — |
| BL2 MCUboot 启动 | 读 `0x2809f000` telemetry 区是否写入交接标记 | AP 启动向量是否跳到 `0x28040000+`(SRAM3/AP 区) |
| CP/AP SMP 上线 | SPINLOCK section 占用计数（应 ≤512+128） | RPMsg carveout / share_mem 基址 |

### C.2 命令模板（JLinkExe，read-only）
```
JLinkExe -device BK7258 -if SWD -speed 4000
> connect
> halt            // 仅暂停，不写
> mem 0x2809F000 0x100   // telemetry 交接区
> mem 0x2809FFF7 0x10    // CPU2 IPC 地址
> mem 0x28000000 0x40    // SRAM0 VT（CP 当前 PC/SP）
> mem 0x44000600 0x10    // AON_WDT 当前 period/key 状态
> mem 0x2800C000 0x40    // boot_ota_ramfunc
> go              // resume
```
对 AP 核：`JLinkExe -device BK7258 -if SWD` 后用 `coresight`/`exec` 切核（依 J-Link 多核支持），或分别 attach CPU1/CPU2。

### C.3 判定标准（与官方一致=通过）
- `0x2809FFF7` 是 ATE 专用地址，不能预设为通用 CP↔CPU2 握手字。一般固件应
  以 `APBS @0x2809f000` 和 `CPU2 @0x2809f180` 的 magic、state、error、向量
  与核 ID 为运行期判据。
- SPINLOCK 保留区保持在 `0x28000000..0x2800ffff`，且不与 CP/AP 运行区重叠；
  单次内存快照不能推出“占用了多少把锁”，不再使用伪计数判据。
- 当前 direct-XIP 配置允许初始 VTOR/入口位于 AP Flash XIP 区，并把运行期向量、
  MSP/heap 放入团队已验证的 `0x28050000..0x2809f000` AP SRAM；不要套用
  BK7236 单核或另一官方分区配置的固定 SRAM 地址。
- `0x2809f000` telemetry 区交接标记与 `n17-signed-manifest-abi.md` 记录一致。
任何不符 → 触发 ADR-022「reversal signal」：隔离该阶段并比对 Ghidra 官方反汇编。

### C.4 2026-08-08 实测

- J-Link 只枚举 AP[0]，未取得 AP 核独立寄存器窗口；共享 SRAM 快照成功。
- `APBS`：state=2、error=0、physical ID=1、initial VTOR=`0x02150200`、
  AP RAM=`0x28050000..0x2809f000`、clock=120 MHz。
- `CPU2`：state=7 (`SECONDARY_READY`)、error=0、local/physical ID=1/2、
  vector/runtime VTOR=`0x02150400`、MSP=`0x2809f000`。
- CPU1/CPU2 control=`0x02150239/0x02150439`。
- `0x2809fff4=0xaaaaaaaa`，所以 byte `0x2809fff7=0xaa`；这否定了原先把
  ATE 地址当作通用握手字的假设，但不否定 `APBS`/`CPU2` 双核上线证据。

---

## SOP-D：OTP shadow 只读交叉验证

目的：把 P4 的「基址证据」升级为「运行期读值证据」——确认 BL1 的
security-counter 解码读到的就是真 BK7258 OTP 内容。

### D.0 官方 OTP/EFUSE 基址（SDK reg_base.h，已验证）
- `SOC_OTP_REG_BASE    = 0x4b100000`
- `SOC_OTP_AHB_BASE    = 0x4b010000`
- `SOC_OTP_APB_BASE    = 0x4b100000`
- `SOC_EFUSE_REG_BASE  = 0x44880000`
- `SOC_MPC_OTP_REG_BASE= 0x41130000`

### D.1 读值（read-only，不改）
```
JLinkExe -device BK7258 -if SWD
> halt
> mem 0x4b100000 0x200    // OTP 区（含 security_counter 位图）
> mem 0x44880000 0x100    // EFUSE 区
> go
```

实际策略读取使用 Dubhe shadow，而不是把 `0x4b100000` 控制器窗口本身当作
OTP 数据。最小的非密钥复查地址是：

```
mem32 0x4b110400,1   // OTP_SET
mem32 0x4b110410,1   // OTP_UPDATE_STAT
mem32 0x4b111028,8   // Secure-Boot public-key hash shadow
mem32 0x4b111068,1   // LCS
mem32 0x4b11107c,1   // lock control
mem32 0x4b111088,1   // BL1 counter bitmap
mem32 0x4b111100,0x10 // BL2 counter bitmap (64 B)
```
### D.2 与 BL1 解码交叉验证
- 取 `0x4b100000` 区中 security_counter 位图，按 `boot_bl1_policy.c` 的
  `bk7258_bl1_security_counter_decode()`（unary 一位计数）解码，得到 counter 值。
- 比对 `n17-signed-manifest-abi.md` 记录的 Manifest `security_counter @0x020`
  （8B 单调放行值）与板读 counter 是否单调一致。
- 比对 `bl1_control` 向量页（`make_bk7258_bl1_control.py` 生成）中写入的 floor。
- 若板读 OTP 为空（shadow 未编程，符合「recoverable dev」边界）→ BL1 走
  软件 fallback floor（`BK7258_BL1_MANIFEST_MIN_IMAGE_VERSION`），记录该状态。

### D.3 判定标准
- 板读 OTP 位图经 unary 解码 == BL1 运行期 floor 来源 → 「基址证据」升级为
  「运行期证据」，BL1 的 BK7258 化 100% 坐实。
- 板读 OTP 全 0/空 → 确认当前为软件 fallback 模式（设计内，SB-H 前预期）。
- 若发现非 0 且 decode 与 Manifest 冲突 → 立即 halt 并升级为 ADR-022 reversal signal。

### D.4 2026-08-08 实测

- `OTP_SET=1`、`OTP_UPDATE_STAT=4`；
- Secure-Boot public-key hash shadow 全零，LCS=0，lock-control=0；
- BL1 counter bitmap=0，按连续低位 1 解码得到 OTP floor=0；当前软件 minimum
  为 1，因此 BL1 effective floor=1；
- BL2 64-byte counter 全零，popcount floor=0，当前编译 floor 也为 0；
- 结论：当前样片是未配置硬件根的可恢复开发态，BL1 使用软件公钥 fallback。
  本次只读未访问设备根密钥字段，也未改变任何 OTP/EFUSE 状态。

---

## 注意
- WSL2 通过 Windows 路径调用 SEGGER J-Link；Linux `which JLinkExe` 为空不再
  等同于“没有 J-Link”。当前设备以 `CORTEX-M33` 配置连接后被识别为 STAR r1p0，
  J-Link 会报告 core-name mismatch，但只读 AHB-AP 访问可用。
- 任何 `mem read` 之外的行为（erase/write/unlock）均超出本次红线，禁止执行。
