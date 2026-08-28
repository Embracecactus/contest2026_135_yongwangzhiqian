> **Historical / retired:** this file records evidence for the former custom
> N15 OTA lifecycle. Its runtime implementation and dedicated verification
> scripts were removed on 2026-08-10; it is not the active update design.

# BK7258 N15 OTA / AB source verification

> 日期：2026-08-03
> 状态：**HISTORICAL / ADR-003 R1/R2 rejected-option evidence**
> 对应Stage：[15-n15-tier2-ota.md](prompts/15-n15-tier2-ota.md)
> 自动门禁：`verify_bk7258_ota_layout.py`、`inspect_bk7258_rbl.py`、
> `simulate_bk7258_ota_journal.py`、`bk7258_ota_metadata.py`、
> `verify_bk7258_ota_sram.py`

> **状态勘误：**本文记录的是后来被否决的 ADR-003 sector-swap 路线，保留其
> source/model 证据，不再是 active implementation guide。项目已接受
> [ADR-004](../../../../memory/decisions/ADR-004-n15-official-contiguous-ab-layout.md)，
> 并完成 [N15-M 新布局板端迁移验证](../../../../progress/verification/2026-08-03-n15-migration-board-verification.md)。
> 本文中的“推荐”“下一步”和旧地址不得用于当前构建、恢复或烧录。

## 1. 结论

N15-R1只使用官方Beken SDK v3.1.1.9，确认了以下事实：

1. v3.1.1.9 RBL是96-byte header，不是旧文档所写的`magic@+0x08 + SHA-256/signature`格式；
2. official packager使用CRC32和32-bit FNV-1a。它提供非加密完整性检查，不提供publisher
   authenticity；
3. official app_ab把连续primary CP/AP映成同尺寸连续`s_app`，由Flash controller一个offset切换；
4. official boot state machine提供一次未确认trial并在下一次boot回到旧partition；
5. 项目现有CP/LittleFS/AP布局无法用一个offset映射到B区，直接复制official AB方案会失败；
6. official `bk_flash_*`接收raw physical offset；原team packer却把LittleFS边界按34/32 XIP展开为
   `0x110000`，与MTD实际起点`0x100000`形成64 KiB潜在重叠；
7. team安全门禁已收紧：CP safe physical slot为`0xef000`、CRC blob上限`0xeeff0`、raw logical
   image上限`0xe0f00`；当前小镜像未触发历史上界；
8. 8 MiB Flash仍可容纳bounded-safe CP/AP staging、四份`0x13000` directional/mirrored
   journal copy和一个scratch sector，并保留`0x5e000`未分配空间；
9. exact v1 metadata ABI已在C/Python间冻结，header为`0x100`、marker为`0x20`，精确字节级
   corruption/torn-write测试和32,915-case recovery model均PASS；
10. team Tier-1已链接一个`0x680`-byte SRAM Flash闭包，外部call/XIP literal均为0、静态entry
    stack上界176 bytes，且正常boot路径不可达、source/binary write gate均为0；
11. 因此当前推荐的是保持现有地址的paired physical-sector swap，而不是重分区/remap；R2技术证据
    已闭环，但ADR仍是Proposed，尚未授权任何Flash写入。

## 2. 唯一SDK基线与provenance

| 输入 | 路径 | SHA-256 / 状态 |
|---|---|---|
| official source | `/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9` | active baseline only |
| source archive | `/mnt/c/Users/lijian/Downloads/BK7258_SMP/bk_avdk_smp-release-v3.1.1.9.tar.gz` | preserved input |
| normal boot | `cp/components/bk_libs/bk7258/bootloader/normal_bootloader/bootloader.bin` | `105161bb603eedafbffcb5efb8f7c06a0c8503e42ba4da46490c2c21ed813de6`, 52352 B |
| AB boot | `cp/components/bk_libs/bk7258/bootloader/ab_bootloader/bootloader.bin` | `3b27958ef78cbb7e56b57695585008465c759a7671cfd776334fec49d3164047`, 18720 B |

旧SDK archive不参与本阶段分析、构建或验证。两个binary hash已固化进layout verifier；传入其他内容会
fail-closed。

## 3. 官方source路径

| source | 已确认语义 |
|---|---|
| `tools/env_tools/rtt_ota/ota-rbl/ota_packager_python.py` | `gethead()`按固定顺序构造96-byte header；algorithm 0时body/raw相同；AB header位于logical container末4 KiB起点 |
| `tools/build_tools/build_process/bk_sdk/bk_ota_pack.py` | AB先线性合并所有execute app，RBL pack后再做32+2 CRC expansion，并覆盖bootloader之后的all-app内容 |
| `tools/env_tools/bk_py_libs/bk_ota_partition/bk_ota_partition.py` | 所有execute app合并为`appa`；`s_app`与execute partition使用`beken_onchip_crc` |
| `projects/app_ab/partitions/bk7258/auto_partitions.csv` | primary CP/AP连续，`s_app`大小等于两者之和；官方tail固定在`0x7fa000`起 |
| `ap/components/ota/ota_common.c` | byte 0/4/8/12 flag写法、current partition、confirm和download状态 |
| `ap/components/ota/ota_base_drv.c` | 当前运行A时写B、运行B时写A；写入时逐sector erase并read-back |
| `cp/middleware/driver/flash/flash_partition.c` | 只把执行函数logical地址转physical做权限判断；partition start原样传给`bk_flash_*` |
| `cp/components/bk_startup/system_main.c` | AP boot前把physical partition start按34→32转换，反证partition table/API使用physical offset |
| `tools/env_tools/beken_utils/scripts/gen_security.py` | 冻结`FLASH_VIRTUAL2PHY`/`FLASH_PHY2VIRTUAL`的32↔34公式 |
| `cp/include/driver/flash.h`、`flash_driver.c`、`flash_notify.c` | erase标记ITCM，但write/read/lock/RTOS/可配置notify不是完整SRAM闭包；pinned sdkconfig关闭Flash MB |

`projects/app_ab/.../ota_rbl.config`中的demo key/IV没有被复制、记录或当成secure key；当前AB配置
`gzip=0/aes=0`。

## 4. RBL binary/source交叉验证

官方Python的精确布局是：

```text
0x00  char magic[4] = "RBL\0"
0x04  uint16_t algorithm
0x06  uint8_t timestamp_raw[6]
0x0c  char app_partition[16]
0x1c  char download_version[24]
0x34  char current_version[24]
0x4c  uint32_t body_crc32
0x50  uint32_t raw_fnv1a
0x54  uint32_t raw_size
0x58  uint32_t stored_body_size
0x5c  uint32_t header_crc32
0x60  end
```

normal bootloader Ghidra交叉验证：

- `0x02001adc`读取`0x60` bytes header；
- `0x02001f1c`从body offset`0x60`计算stored-body CRC32；
- `0x02002050`是FNV-1a step；
- `0x02002070`按header`+0x54`长度计算并与`+0x50`比较。

AB bootloader Ghidra交叉验证：

- `0x02002f1c`从partition物理末尾`0x1100`读取logical末4 KiB起点的`0x60` bytes header；
- `0x02002070`检查`RBL\0`并调用hash校验；
- `0x02002e70`按header raw size计算FNV-1a；
- boot fallback主要依赖magic/FNV；download端的CRC检查不能被误写成boot端签名认证。

`inspect_bk7258_rbl.py`已经通过：

- synthetic prefix + AB-tail positive；
- 两种body corruption negative；
- 官方v3.1.1.9 packager生成的algorithm-0 prefix RBL正向复核。

## 5. 官方AB remap与state machine

AB bootloader关键函数：

| address | 逆向语义 |
|---:|---|
| `0x020022d8` | 读写Flash controller `base + 0x64` offset-enable bit |
| `0x020024f0` | 由`appa`/`s_app`计算并写`base + 0x58/+0x5c/+0x60`映射参数 |
| `0x02002728` | 读取final/temp/confirm/download flags，处理first boot、trial、未确认回退和normal boot |
| `0x02002304` | 首次启动时选择有效A/B并初始化flag/remap |
| `0x020023a0` | 当前partition hash失败时验证另一partition并切换，否则halt/reboot |

pending update的核心行为是：

```text
app A stages B -> metadata marks target B + pending
boot validates B -> temporarily runs B while metadata remembers fallback A
B confirms -> subsequent boots remain B
B resets before confirm -> next boot selects A
```

本项目需要保持该“一次trial、未确认回退”的产品语义，但不能直接复用单offset数据面。

## 6. 地址域纠错与布局证明

旧文档把logical XIP partition和raw physical API地址混为一谈：

- linker logical：CP `0x10000..0x100000`、data `0x100000..0x200000`、AP
  `0x200000..0x400000`；
- executable physical：CP从`0x11000`开始、AP从`0x220000`开始；
- MTD raw physical：LittleFS实际为`0x100000..0x200000`。

原packer以`0x110000`作为LittleFS physical boundary，故其声明CP tail
`0x100000..0x110000`与真实LittleFS重叠。修正后clean v3.1.1.9 N14-profile rebuild的CP
padded CRC为`0xb0000`，从`0x11000`结束于`0xc1000`，距真实边界还有`0x3f000`。所以这是一项
latent boundary bug，不是对既有实板数据已损坏的主张。

纠错后的A pair physical slots：

- CP safe：`0x011000..0x100000`，`0xef000` bytes；
- AP：`0x220000..0x440000`，`0x220000` bytes；
- combined：`0x30f000` bytes。

`0x440000..0x7fa000`可用空间为`0x3ba000`，足以保存bounded-safe pair，并剩余`0xab000`。
候选pair后给正向/反向各两份`0x13000` journal copy和一个scratch sector，仍保留`0x5e000`
未分配空间。CP的最大完整34-byte CRC blob为`0xeeff0`，对应最大raw logical image
`0xe0f00`。

若保持A内相对位置并用单offset镜像，B span需`0x42f000`，从`0x440000`延伸到
`0x86f000`；相对官方tail boundary超出`0x75000`。该结论由layout verifier直接计算，不依赖
人工十六进制估算。

R2已经修正team-owned的三道fail-closed门禁：

- `postbuild.sh`把CP raw image最大值从`0xf0000`收紧到`0xe0f00`；
- `pack_dual_image.py`把raw LittleFS边界从`0x110000`改为`0x100000`；
- `bk7258_auto_debug.sh`在调用loader前同样拒绝跨越`0x100000`的CP segment。

`bk7258_amp.h`现在显式区分logical XIP、CRC-expanded executable physical和data raw physical
常量；MTD wrapper只使用后者。未改官方NuttX/apps/SDK。

## 7. Flash执行闭包证据

official source和当前N14 ELF/map证明，直接调用SDK static library不等于安全的boot swap engine：

- `bk_flash_erase_sector`声明在`.itcm_sec_code`，但`bk_flash_write_bytes/read_bytes`、
  `flash_lock/unlock`、no-lock helpers和`mb_flash_op_prepare/finish`位于`.text.*`；
- 当前N14 map中write/read/MB symbols位于`0x02034xxx..0x02036xxx` XIP，orphan
  `.itcm_sec_code`本身也位于`0x020a999c`，没有被team linker搬到SRAM；
- R2实现前team Tier-1把`.text*`全放Flash且没有ramfunc copy过程；现在只为新的team-owned
  `.ota_sram`增加了独立VMA/LMA和inactive loader，现有SDK调用链仍未被搬入；
- exact v3.1.1.9 `sdkconfig`关闭`CONFIG_FLASH_MB`，但高层driver仍依赖RTOS critical/lock/task
  语义；未来若切换配置也不能假定NuttX AP实现vendor `MB_CHNL_FLASH`协议；
- team boot在进入main前已`cpsid i`且主动保持AP核off，WDT约8秒。未来engine必须维持该隔离，
  并把Flash controller routine、literal、data、stack和WDT feed组成一个可审计SRAM闭包。

因此R2结论不是“SDK Flash函数可直接复用”。team-owned Tier-1现在已经实现并链接了最小SRAM
engine；它只参考v3.1.1.9 source/register语义，不修改SDK，也不调用SDK/RTOS/mailbox/libc路径。
该实现仍由零值硬门禁禁用，且正常boot路径不可达。

### 7.1 Journal program-unit与host recovery model

official v3.1.1.9 `flash_write_common()`使用`FLASH_BYTES_CNT=32`，把调用者请求合并到填充为
`0xff`的32-byte buffer后提交完整block。这里的32 bytes是SDK write chunk，不是对集成Flash
物理program unit的主张。BK7258的Flash集成在芯片/封装内；实板读取的JEDEC兼容ID是
`0xC86517`，与GD25WQ64E公开表中的`C8 65 17`相符，而GD25Q64E实际是`C8 40 17`。
这只能作为接口/命令集旁证，不能推导出板上有独立GD25WQ64E，也不能让外部器件datasheet覆盖
Beken规范。当前proposal以Beken official v3.1.1.9 driver和实板行为为硬依据，并保守地让每个
phase marker独占一个32-byte SDK write chunk，不依赖同一chunk的重复局部写。

- CP/AP pair共有239 + 544 = 783个erase sector；
- 每个sector有scratch-ready、active-replaced、staging-replaced三个phase；
- 每个方向有2349个marker，连同`0x200` immutable header/control共需`0x127a0`；
- 每份日志向4 KiB取整为`0x13000`（19 sectors）；
- forward/reverse各两个镜像copy，总计76个journal sectors，另有一个scratch sector；
- proposal布局为`0x74f000..0x79c000`，其后`0x79c000..0x7fa000`仍保留`0x5e000`。

read-only simulator现在使用精确v1 header/marker字节，在正反两个方向对每个sector/phase的erase、torn program、complete program、
两份marker提交断点注入reset，并覆盖journal activation、trial-start、confirm、uint64 generation
wrap、marker gap和坏staging/metadata。32915个case全部PASS：确认后`active=new,
staging=old`；未确认则反向恢复到`active=old, staging=new`。这是协议模型证据，不是SRAM engine、
真实Flash时序或板端掉电证据。

### 7.2 Exact metadata ABI与SRAM闭包

repository-owned `boot_ota_abi.h`和`bk7258_ota_metadata.py`精确镜像little-endian v1 ABI：

| object | 大小 | CRC位置 | 关键约束 |
|---|---:|---:|---|
| immutable header | `0x100` | `0xfc` | 固定Flash/layout/image encoding、uint64 sequence/generation、pair/slot digest、pre-status，reserved必须为`0xff` |
| control/phase marker | `0x20` | `0x1c` | 仅完整精确record提交；torn prefix无效 |

四份日志header内容相同，direction/copy由物理地址推导。SHA-256/CRC32只作corruption detection，
不宣称publisher authentication或anti-rollback。self-test拒绝256个header单字节破坏、32个marker
单字节破坏，并正确分类32个torn marker prefix。

Tier-1 linker把`.ota_sram`映射为SRAM VMA `0x28000000`、boot-Flash LMA `0x02000c40`；当前section
为`0x680` bytes，预留上限`0x2000`。自动验证证明：

- closure内所有required symbol均驻留SRAM，外部call为0，XIP pointer literal为0；
- `.data/.bss`为空，entry静态stack上界176 bytes（门限512）；
- normal `Reset_Handler/c_main`没有调用`boot_ota_engine_call`，installer只被该inactive wrapper引用；
- source宏与linked gate word均为0，输出固定`writes_enabled=false`；
- entry要求`PRIMASK=1`、D-cache/MPU关闭、request/MSP在boot SRAM、CPU1/CPU2 power-down；
- wait/WDT/fail-reset、ID/status、read/program/erase/copy、地址白名单和每次read-back均在闭包内；
- controller可能卡住时停止feed并在SRAM等待WDT reset，不返回XIP。

verifier还对exact v3.1.1.9 Flash driver/LL/register、SYS secondary-core、WDT register和sdkconfig
做source-fragment与SHA-256检查。C86517保护处理只修改BP/CMP位并保留其余status位；未来parser必须
把reset后的保护恢复纳入recovery。完整证据见
[N15-R2 verification](../../../../progress/verification/2026-08-03-n15-r2-sram-metadata.md)。

参考：

- [Beken BK7258产品页（Flash最高16 MB）](https://www.bekencorp.com/index/goods/detail/cid/60.html)
- [GigaDevice GD25WQ64E产品页（兼容性旁证）](https://www.gigadevice.com/product/flash/spi-nor-flash/gd25wq64e)
- [GD25WQ64E Rev1.2 datasheet（`C8 65 17`）](https://download.gigadevice.com/Datasheet/DS-00476-GD25WQ64E-Rev1.2.pdf)

## 8. R1/R2只读工具门禁

```text
python -m py_compile (all N15 tools)         PASS
inspect_bk7258_rbl.py --self-test            PASS
inspect official-generated plain RBL         PASS
verify_bk7258_ota_layout.py --sdk-source ... PASS
simulate_bk7258_ota_journal.py                PASS (32915 cases)
bk7258_ota_metadata.py --self-test            PASS
bootloader make verify + exact SDK source     PASS (gate=0, SRAM=0x680, stack=176)
cp_nsh_psram + ap_smp_psram clean build      PASS (v3.1.1.9 only)
existing-artifact packer positive/oversize   PASS / expected rejection
```

layout verifier还检查：

- 修正后的`pack_dual_image.py`、post-build与debug SOP都在raw `0x100000`前拒绝CP越界；
- MTD明确使用raw physical constants，official source contract仍是raw-address API；
- 所有候选区域4 KiB对齐、连续、不重叠；
- B CP/AP分别与A bounded-safe slot等长；
- official tail四个partition地址精确匹配；
- normal/AB boot binary hash精确匹配v3.1.1.9；
- journal model会执行精确metadata ABI测试并固定32,915-case覆盖；
- Tier-1 source和binary SRAM mutation gate均为0，正常boot path不可达；
- 输出明确`writes_enabled=false`和`compatible_with_current_layout=false`。

clean build同时通过SDK CP/AP checksum、RPTUN layout、BLE GATT和PSRAM verifier。产物大小为：

- CP raw/CRC/padded：`676820 / 719134 / 720896 (0xb0000)` bytes；
- AP raw/CRC/padded：`173640 / 184518 / 188416 (0x2e000)` bytes。

这些是host build证据，不是board boot/Flash验证。

## 9. R2退出与后续未决项

R2的raw controller source closure、SRAM link/copy/stack证明和metadata ABI已完成，且所有工具仍
fail-closed为`writes_enabled=false`。R2技术退出条件满足；ADR-003仍需owner明确接受或拒绝，不能由
实现者自动改为Accepted。

后续stage仍必须完成：

1. N15-A：CP/AP pair manifest、deterministic bundle/parser、version policy和完整负例；
2. N15-B：CP-only staging owner/quiesce、全slot read-back和pending publication；
3. N15-C/D/E：boot parser、保护恢复、journal resume、one-trial health/confirm/revert；
4. N15-F：controller timeout/WDT预算、全pair digest耗时、固定phase实板reset/power-loss注入；
5. wear/time基线、失败后的人工recovery与全量N14回归。

这些门禁继续阻止任何staging或swap写路径启用；接受ADR也不等于允许把write gate改为非零。
