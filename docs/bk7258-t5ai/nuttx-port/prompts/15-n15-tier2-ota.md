> **历史阶段（已退役）**：本文保留 N15 的任务定义和验收证据。对应自定义
> OTA 状态机、写入器和验证脚本已于 2026-08-10 从现役源码删除；不得据此
> 恢复第二套更新框架。

# BK7258 Stage N15：成对 CP/AP OTA、回退与掉电恢复

> 日期：2026-08-04
> 状态：**COMPLETE / 批准的N15最小范围已board-verified：N15-M、format-2 physical A→B→A、两槽回归、RTS与post-confirm完整掉电恢复均通过**
> SDK：只使用 official Beken v3.1.1.9
> 架构决策：[ADR-004](../../../../memory/decisions/ADR-004-n15-official-contiguous-ab-layout.md) / [ADR-005（历史format 1）](../../../../memory/decisions/ADR-005-n15-boot-selector-metadata-v1.md) / [ADR-006（当前format 2）](../../../../memory/decisions/ADR-006-n15-symmetric-dual-bank-ota.md)
> 历史被否决方案：[ADR-003](../../../../memory/decisions/ADR-003-n15-paired-sector-swap.md)

## 1. 当前结论

项目已从 N14 的 CP/LittleFS/AP 分散布局迁移到与 official v3.1.1.9
`app_ab` 一致的连续 CP/AP A/B 几何。迁移由 team-owned linker、bootloader、packer、
loader preflight 和 verifier 完成；official NuttX、apps、SDK source 和 SDK static
libraries 没有修改。

N15-M 已完成一次经 owner 明确授权的破坏性迁移。旧 LittleFS 内容已清空，新 LittleFS
已在 `0x600000..0x700000` 自动格式化、挂载并通过重启持久化探针。迁移完成时B只是与A同
generation的不可选择seed；随后format-2 validation profile已经在同一布局上完成真实双向生命周期。因此：

- 新布局基线已经 `board-verified`；
- N15-A exact RBL/pair bundle 已 `host-verified`；
- N15-B candidate staging 已 `host/source/ELF-verified`，随后由实板双向流程验证；
- N15-C boot pair selector/one-offset remap 已 `host/source/ELF-verified`；
- N15-D one-trial append/read-back、confirm和rollback已
  `host/source/ELF-verified`；
- N15-E pending publication和metadata sector reclamation已
  `host/source/ELF-verified`，N15-F health-confirm、独立validation profile和volatile PSRAM
  transport也已完成host/source/ELF收口；
- N15-V target-only one-shot failpoint 7/12和ordered generation 42..57的16份独立
  artifact、format-2 campaign独立校验和16次loader dry-run均PASS；generation 56确认B，
  terminal generation 57从已确认B回切并确认A；
- normal profile六个Boot门为零且没有可启用写入的CLI/runtime路径；validation profile才编入写路径，runtime gate仍
  从false开始并要求generation token；
- generation 314已从A写入B并经bank 0进入confirmed B；generation 315再从B写回A并经bank 1进入confirmed A；两个方向的full-slot read-back/SHA、trial boot和保留服务回归均PASS；
- physical run没有专门触发未confirm rollback；rollback仍是host reset-boundary/fault证据；
- confirmed-A RTS和完整移除USB/J-Link供电后的恢复均通过；两次都读取到同一generation 315 confirmed-A状态，完整掉电后AP/CPU2/RPTUN也健康。
- 板端随后通过三个有界segment恢复normal `cp_nsh_psram + ap_smp_psram`；B、双metadata、LittleFS、`usr_config`和tail未进入写集合，post-flash AP/CPU2/RPTUN、LittleFS探针和PSRAM均PASS，`bkota`命令不存在。

## 2. 已冻结布局

| 区域 | raw physical range | 大小 | 当前策略 |
|---|---:|---:|---|
| bootloader | `0x000000..0x011000` | `0x011000` | team source，official envelope |
| CP A | `0x011000..0x165000` | `0x154000` | generation 315，最后观测为active/stable |
| AP A | `0x165000..0x286000` | `0x121000` | generation 315，最后观测为active/stable |
| CP/AP B (`s_app`) | `0x286000..0x4fb000` | `0x275000` | generation 314，当前inactive |
| trial metadata bank 0 | `0x4fb000..0x4fc000` | `0x001000` | generation 314 confirmed-B历史bank |
| vendor `usr_config` | `0x4fc000..0x50a000` | `0x00e000` | loader migration 不覆盖 |
| trial metadata bank 1 | `0x50a000..0x50b000` | `0x001000` | generation 315 confirmed-A当前selected bank |
| reserved | `0x50b000..0x600000` | `0x0f5000` | 不覆盖 |
| CP LittleFS | `0x600000..0x700000` | `0x100000` | 已清空并重新建立 |
| reserved | `0x700000..0x7fa000` | `0x0fa000` | 不覆盖 |
| official tail | `0x7fa000..0x800000` | `0x006000` | loader/OTA 禁止覆盖 |

XIP 地址：

- CP：`0x02010000..0x02150000`；
- AP：`0x02150000..0x02260000`；
- AP CPU0/CPU1 vectors：`0x02150000` / `0x02150200`。

迁移不是 chip erase。loader 只接收两段：

```text
all-app-factory.bin@0x000000-0x4fc000
littlefs_factory_clear.bin@0x600000-0x100000
```

把它做成两段而不是一个稠密 `0x700000` 文件，是为了让 `usr_config` 与两个 reserved
区间根本不出现在写集合中。

## 3. 主机门禁

`bk7258_ab_layout.py` 是 canonical host model；
`verify_bk7258_ota_layout.py` 对照 exact v3.1.1.9 official CSV，并交叉检查 team header、
linker、postbuild、boot FAL、MTD、packer、debug SOP 与 factory verifier。

`verify_bk7258_factory_layout.py` 逐字节验证：

- A 中 boot/CP/AP 文件、padding 与物理上界；
- B 是 A 的 byte-exact pair copy，且仍不可启动；
- metadata 为 `0xff`；
- LittleFS clear image 精确为 1 MiB `0xff`；
- write ranges 精确为两段，排除 `usr_config`、reserved 与 calibration tail；
- manifest 中所有长度、offset 与 SHA-256 匹配实际文件。

exact v3.1.1.9 `cp_nsh_psram + ap_smp_psram` clean build 已通过：SDK CP/AP checksum、
bootloader、RPTUN layout、BLE GATT、PSRAM 与 factory byte verifier 全部 PASS。
最终收口还证明两个负例会 fail-closed：截短 LittleFS clear image 因 SHA-256 drift 被拒绝，
错误/旧 layout ID 因 layout drift 被拒绝。

迁移产物：

| artifact | length | SHA-256 |
|---|---:|---|
| `all-app-factory.bin` | `0x4fc000` | `4722e2a81504e5e321f67850c518b0b919b79e796214481d1e0dd01bf9cf8e4b` |
| `littlefs_factory_clear.bin` | `0x100000` | `f5fb04aa5b882706b9309e885f19477261336ef76a150c3b4d3489dfac3953ec` |
| `s_app_seed.bin` | `0x275000` | `aa89797aa90bca393061ed100ca03c5cbed7194fb592cc86f60a7fe8af263d4d` |

## 4. N15-M 板端证据

2026-08-03 22:26 通过 COM7/BKFIL 完成两段写入，两段均
`EraseFlash -> pass`、`WriteFlash -> pass`、`Writing Flash OK`。UART 到
`PASS_NSH`。随后验证：

- AP `READY/error=0`，VTOR `0x02150000`；
- CPU2 `SCHEDULER_ONLINE/error=0`，vector `0x02150200`；
- RPTUN `CONNECTED`，supervisor `HEALTHY`；
- LittleFS 首次 autoformat 后 `BK7258LFS-OK`，3 次 physical reset 后仍可读取；
- PSRAM info + heap 256/256 PASS，AP CPU0/CPU1 16/16，free 稳定；
- SDK timer 256/256 PASS；
- RPMsg 六场景×20 PASS，heap 稳定；
- RPMsgFS 四档×1 PASS；
- Bluetooth info PASS，BD_ADDR `c8:47:8c:47:47:48`、fallback 0；
- COM7 RTS physical reset 3/3 均 `cold_path=yes`、`PASS_NSH`。

迁移前完整 8 MiB 曾以 6 Mbps 读取，但重复分析证明 BKFIL 高速 read 会偶发插入
128-byte 全零块，所以该 dump 只作 forensic reference，禁止直接回刷。迁移后
`usr_config` 与 official tail 使用 115200 连续两次读取，分别 byte-identical；其 SHA-256 为
`d078e2a2...e79acf` 与 `fa92844a...58c77`。完整证据见
[N15-M verification](../../../../progress/verification/2026-08-03-n15-migration-board-verification.md)。

## 5. N15-A host pair bundle

N15-A 已在 team-owned Python wrapper 中完成，不修改或复制 official SDK/NuttX
源码：

- `pack_bk7258_ota_pair.py` 生成 deterministic CP/AP pair body、exact 96-byte
  v3.1.1.9 algorithm-0 RBL、完整 `0x250000` logical container、32+2 CRC 展开后的
  exact `0x275000` `s_app-candidate.bin` 和规范化 manifest；
- `verify_bk7258_ota_pair.py` 解码并逐 packet 验证 CRC16，重新构造 canonical
  bundle，再交叉检查 layout、address、size、vector、digest、version 和 CP/AP
  generation；
- RBL header 位于 logical `0x24f000`，保留 official `app` 和固定
  `current_version=00010203040506070809`；timestamp 必须显式传入，保证相同输入字节确定；
- manifest schema 为 `bk7258-cp-ap-pair-v1`。当前所有 write/select/remap/trial/board
  gate 均为 `false`，bundle 不能直接授权烧写或启动；
- official header/CRC 独立 golden vector、exact v3.1.1.9 source hash、两次 deterministic
  build、真实 `cp_nsh_psram + ap_smp_psram` bundle，以及 2 positive/13 negative
  self-test 全部 PASS；
- clean dual build 同时通过 factory、RPTUN、BLE GATT、PSRAM 和 SDK checksum gates，
  official NuttX/apps 无 tracked diff。

Host 用法（generation/version/timestamp 必须由调用者显式给出）：

```bash
python3 board/bk7258/scripts/pack_bk7258_ota_pair.py \
  --cp-raw "$WORKSPACE/nuttx/bk7258-dual/app.bin" \
  --ap-raw "$WORKSPACE/nuttx/bk7258-dual/app1.bin" \
  --output "$WORKSPACE/nuttx/bk7258-dual/n15-pair" \
  --generation 16 --version 1.0.1 --base-version 1.0.0 --timestamp 0

python3 board/bk7258/scripts/verify_bk7258_ota_pair.py \
  --bundle "$WORKSPACE/nuttx/bk7258-dual/n15-pair" \
  --expected-generation 16 --expected-version 1.0.1 \
  --expected-base-version 1.0.0 --expected-timestamp 0 \
  --sdk-source "$BK7258_SDK_SOURCE"
```

完整记录见
[N15-A host verification](../../../../progress/verification/2026-08-03-n15-a-host-pair-bundle.md)。

## 6. N15-B CP-only staging

N15-B 已在 team-owned wrapper/core 中完成主机、源码和最终 ELF 收口：

- deterministic 384-byte descriptor 固定 schema、layout、generation、timestamp、version、
  base-version、地址、长度及 physical/logical/CP/AP digests；
- mutation 前完整验证 `0x275000` candidate：32+2 CRC、RBL、vectors、CP magic、所有
  padding 与 digest 任一不符都 fail-closed；
- CP-only shared Flash guard 将 staging 与 MTD/LittleFS owner 串行化，read-only MTD 不获得
  SDK write permission；
- 每个 4 KiB sector 执行 source-read、erase、erased-readback、256-byte program/read-back，
  最后再验证 full-slot SHA-256；
- portable harness 通过 2 positive + 21 negative，覆盖 gate、timeout、lock、erase/program/read、
  source mutation、descriptor/address/size/generation/version/CRC/RBL corruption；
- exact v3.1.1.9 `cp_nsh_psram + ap_smp_psram` 完整构建和 final ELF verifier PASS；
- compile/runtime staging gate 为零，最终 ELF 无 runtime enable setter/NSH command；未写板，
  metadata 与 remap 均未触碰。

完整记录见
[N15-B host/source/ELF verification](../../../../progress/verification/2026-08-04-n15-b-host-staging.md)。

## 7. N15-C boot selector 与 gated remap（历史 format 1 基线）

N15-C 已在 team-owned Tier-1 中完成主机、源码和最终 ELF 收口：

- [ADR-005](../../../../memory/decisions/ADR-005-n15-boot-selector-metadata-v1.md)
  固定 `BKOTA15C` format 1：4 KiB sector 内 8 个 append-only 512-byte record；
- 首 record 必须为 `PENDING_B`，后续只允许
  `PENDING_B -> TRIAL_STARTED -> CONFIRMED_B | ROLLBACK_A`，identity 不可漂移；
- trusted metadata 对 A 执行 actual CP/AP encoded length、所有CRC16、erased padding、vector、
  CP magic和full-pair SHA-256；B复用N15-B完整descriptor/RBL/digest验证；
- team raw-Flash reader与one-offset remap严格对照exact v3.1.1.9 source/binary；
- `PENDING_B`只表示validated candidate，不允许N15-C remap；只有`CONFIRMED_B`可到remap分支；
- portable harness 通过5 positive + 28 negative、4个SHA-256 vector、`-Werror`与
  GCC `-fanalyzer`；
- exact v3.1.1.9完整dual build、final boot ELF symbol/workspace/四个zero gate全部PASS；
- generation 17 pending metadata只生成在host candidate目录，factory metadata仍全`0xff`；未写板。

完整记录见
[N15-C host/source/ELF verification](../../../../progress/verification/2026-08-04-n15-c-host-boot-selection.md)。

## 8. N15-D one-trial confirm/revert（历史 format 1 基线）

N15-D已完成，不改布局或当前板状态：

1. 在N15-C只读selector之上实现portable one-trial controller；
2. pending A/B完整验证后才append `TRIAL_STARTED`，并必须逐字节read-back成功，才给当前一次boot
   临时B权限；
3. reset看到`TRIAL_STARTED`必须回A，`CONFIRMED_B`才稳定选B，`ROLLBACK_A`明确选A；
4. fault matrix覆盖erase/program/read-back timeout、torn record、dirty gap、sequence overflow和
   每个mutation边界reset；
5. physical staging、metadata mutation、selection/remap gate在host/source/ELF收口中保持零；
   任何板写或remap仍需另行申请owner授权；
6. 签名、key provisioning 与 anti-rollback 仍需单独决策。

portable harness通过4 positive、113 negative和48个逐chunk reset边界；Boot SRAM writer
为`0x284` bytes，零XIP literal escape；final Boot/CP ELF及完整exact-v3.1.1.9 dual build均PASS。
完整记录见
[N15-D verification](../../../../progress/verification/2026-08-04-n15-d-host-trial.md)。

## 9. N15-E pending publication 与 reclamation（历史 format 1 基线）

N15-E在CP Flash guard下实现portable publication controller：完整验证live A/B和规范化
`PENDING_B`后才允许mutation；erased、consumed trial、rollback或结构损坏sector可被有界回收，
trusted旧生命周期要求generation严格递增。4 KiB erase必须read-back全`0xff`，512-byte record按
16个32-byte chunk逐个program/read-back，最后再次解析并逐字节确认。已有pending/confirmed不会
被擦除。

host矩阵通过5 positive、142 negative、8个erase和112个program/reset边界，以及`-Werror`、
GCC `-fanalyzer`、exact v3.1.1.9 contract和normal/validation最终ELF。详见
[N15-E verification](../../../../progress/verification/2026-08-04-n15-e-host-publication.md)。

## 10. N15-F health policy 与 validation transport

confirm要求trusted `TRIAL_STARTED`、expected uint64 generation、secondary mapping active、AP
supervisor healthy/fault-free，并让同一supervisor generation/fault count在目标侧连续稳定
5000 ms（250 ms轮询）；host模型可用1000 ms fixture缩短确定性测试。clock regression、状态
变化或fault会重置窗口。矩阵通过7 positive、15 negative及5次continuity reset。

独立`cp_nsh_ota + ap_smp_psram` profile把Boot六门置1、CP compile write gate打开，但runtime
gate保持BSS false。每个mutation命令要求exact `N15-WRITE-<generation>`并在返回时disarm。
由于0x275000 candidate装不进1 MiB LittleFS，validation profile只在检测到16 MiB PSRAM后使用
固定volatile窗口：candidate `0x60800000..0x60a75000`、descriptor `0x60a75000`、record
`0x60a76000`、end `0x60a76200`。normal profile仍是upper-8 boot-tested/unallocated。

WSL2 loader默认dry-run，实际运行还要求target先完成`prepare-transfer`并显式传
`--watchdog-stopped --execute`；J-Link命令块只有halt/load/verify/go/exit，无Flash命令。
实板暴露的大块/同进程连续load不可靠问题已经通过64 KiB candidate chunk、每块fresh Commander
process、`noreset`和逐块`verifybin`关闭。host基础证据见
[N15-F verification](../../../../progress/verification/2026-08-04-n15-f-host-validation.md)。

N15-V host准备已完成：target failure points覆盖stage/publish/trial read/write/erase，另有
candidate PSRAM单字节受限corruption；7 positive/12 negative harness PASS。campaign packer为
generation 42..57生成16个不同RBL/version/timestamp/metadata身份，逐包verification和loader
dry-run均PASS；独立campaign verifier再次检查exact fault、path containment、三类unique
identity并重跑16份pair/transfer/loader dry-run。format 2要求每个case执行controlled power
cycle；fault case按fail-before callback、quiescent return后断电，明确不声称Flash pulse中途
brownout。generation 56确认B；generation 57以该B pair/version为base，stage inactive A并确认
A，固定为terminal case。独立host资格测试另用generation 300..315，未消耗板端generation。见
[N15-V host verification](../../../../progress/verification/2026-08-04-n15-v-host-fault-injection.md)。

ADR-006 format 2现为当前实现：bank 0 `0x4fb000..0x4fc000`与bank 1
`0x50a000..0x50b000`交替发布，旧selected bank在新record完整read-back前保持有效；状态族为
`PENDING_B/TRIAL_B/CONFIRMED_B/ROLLBACK_A`和
`PENDING_A/TRIAL_A/CONFIRMED_A/ROLLBACK_B`。portable rotation、selector、trial、publish、
control和health矩阵分别通过22/8、9/4、2/6、5/10、4/8、2/6正/负例或恢复例，Boot/CP共用
slot-neutral core，validation完整构建通过。

实板使用generation 314完成A→B、bank 0、trial B、N14保留服务与confirmed B；再用generation
315完成B→A、bank 1、trial A、相同回归与confirmed A。两次inactive pair均写入2576384 bytes并
通过完整Flash read-back/SHA；confirmed-A RTS恢复仍保持generation 315、active A和runtime gates 0。
实板记录见[N15 physical symmetric lifecycle](../../../../progress/verification/2026-08-04-n15-physical-symmetric-lifecycle.md)。

confirmed A之后，owner同时移除USB/J-Link全部供电并重新连接；capture-only COM11验收读取到
generation 315、bank 1、confirmed A、secondary/gates 0，AP/CPU2/RPTUN健康。随机时机或Flash
pulse中途断电不在当前SOP内；physical rollback也没有在这轮最小confirm路径中执行，不能与host
rollback模型混写。

验收完成后，owner批准刷回normal gates-zero镜像。COM7 sparse loader仅写Boot
`0x000000+0x11000`、CP A `0x011000+0xb4000`和AP A `0x165000+0x2e000`，三段erase/write及NSH
启动均PASS；只读回归确认AP/CPU2/RPTUN、LittleFS与PSRAM健康，并确认normal没有`bkota` CLI。

后续门禁：

| 子阶段 | 工作 | 退出条件 |
|---|---|---|
| N15-A | pair bundle + exact RBL/parser | **DONE / host-verified**：2 positive + 13 negative PASS；无板写 |
| N15-B | CP-only `s_app` staging wrapper | **DONE / host/source/ELF-verified**：2 positive + 21 negative；无板写 |
| N15-C | team boot remap + pair validation | **DONE / host/source/ELF-verified**：5 positive + 28 negative；四门为零；无板写 |
| N15-D | one-trial confirm/revert | **DONE / host/source/ELF-verified**：4/113、48 reset boundaries；六门为零；无板写 |
| N15-E | pending publication/reclamation + fault injection | **DONE / host/source/ELF-verified**：5/142、8 erase、112 program/reset；无板写 |
| N15-F | health policy + gated validation/transport | **DONE / host/source/ELF-verified**：7/15、5 continuity resets、完整validation与normal rebuild；无板写 |
| N15-V | deterministic fault campaign + full board regression | **APPROVED MINIMAL PHYSICAL SCOPE DONE**：7/12 host fault、16 identities与dry-run保留；generation 314/315双向trial/confirm、两槽回归、RTS和post-confirm完整VDD removal均实板PASS；analog pulse brownout不在本阶段声明内 |

## 11. 不变量

- 只使用 official v3.1.1.9；legacy 仅保留，N15 完成后另行决定是否验证。
- 不修改 official NuttX/apps/SDK source 或 SDK static libraries。
- CP 与 AP 必须是同一 generation；任何 mismatch 都不得 trial boot。
- normal sparse update必须使用新 offset，并保留B、LittleFS、`usr_config`与tail。
- 禁止 chip erase，禁止把历史 ADR-003 journal/scratch 地址重新接回 active build。
- 禁止运行会使 Windows 卡顿的 `BLEDebug.EXE`。
- ADR-005 metadata v1只保留为历史回归证据；当前ADR-006 format 2的双bank、多代inactive-slot
  A/B轮换已完成最小双向实板验证，完整VDD removal也已通过；physical rollback与analog brownout仍必须按各自证据标签单独声明。
