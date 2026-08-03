# Stage N5 — Flash Layout / ID / MTD / LittleFS Filesystem

> **范围**：read-only flash layout/ID/scan，destructive raw flash erase/write，MTD lower-half，
> ftl block device，LittleFS format/mount/persistence。
> **状态**：N5-D0..D4 board-observed；N5-D5 raw flash erase/write board-verified；N5-D6 MTD lower-half
> read/erase/bwrite board-verified（方案 A SR0 清/恢复）；**N5-D7 LittleFS filesystem board-verified**。
> **下一步**：N5 收口；后续可进 N6（如 procfs/应用/其它外设）。

---

## 0. 前置条件

- Stage N4（DPLL / 480 MHz clock bring-up）尚未整体 `board-verified`；N5 read-only flash
  exploration 不依赖 N4 DPLL 结果，可并行推进。
- 当前可用 artifact：N4-D0/D0D/F 候选 `$FW/all-app.bin` = 164730 B = `0x2837A`（2026-07-18
  构建时间戳 `Jul 18 2026 22:11:54`）。

---

## 1. N5-D0 — Layout Candidate Board-Observed

通过 NSH probe 命令读取 flash layout candidate，板端输出：

```
N5FS:L
FSIZ=00800000 IMGL=0002837a
REND=000fffff
DSTA=00100000 DSIZ=00100000 DEND=001fffff
```

**解读**：

| 字段 | 值 | 含义 |
|---|---|---|
| `FSIZ` | `0x00800000` | 8 MB flash candidate（= T5-AI 标称 flash 容量） |
| `IMGL` | `0x0002837A` | 当前 image length（= `$FW/all-app.bin` 164730 B） |
| `REND` | `0x000FFFFF` | reserved 区结束 @ 1 MB 边界 |
| `DSTA` | `0x00100000` | data candidate 起始（1 MB 偏移） |
| `DSIZ` | `0x00100000` | data candidate 大小 = 1 MB |
| `DEND` | `0x001FFFFF` | data candidate 结束（2 MB 偏移） |

**注意事项**：
- `N5FS:L` 表示 layout mode（L = layout readback）。
- Reserved 区 `0x00000000..0x000FFFFF` 覆盖 bootloader（`0x00000000..0x0000FFFF`）+ app
  区（`0x00010000..0x000FFFFF`，含当前 image + 填充）。
- Data candidate `0x00100000..0x001FFFFF` 在当前 image（`0x2837A` ≈ 163 KB）之外，
  距 image end 约 845 KB，不冲突。

---

## 2. N5-D1 — Flash ID / Geometry Board-Observed

板端 probe 读取 flash JEDEC ID 与 geometry：

```
FID=00c86517 FSIZ=00800000; ESZ=00001000 PSZ=00000100 B64=00010000
```

**解读**：

| 字段 | 值 | 含义 |
|---|---|---|
| `FID` | `0x00C86517` | BK7258集成Flash接口返回的JEDEC兼容ID；命中8 MB NOR geometry |
| `FSIZ` | `0x00800000` | 8 MB（与 D0 layout candidate 一致） |
| `ESZ` | `0x00001000` | 4 KB erase sector |
| `PSZ` | `0x00000100` | 256 B page candidate |
| `B64` | `0x00010000` | 64 KB block |

> BK7258的Flash集成在芯片/封装内，不应描述成板上另焊的独立SPI NOR。实板接口返回的
> `0xC86517`与GigaDevice GD25WQ64E的`C8 65 17`身份及8 MB/4 KB sector/256 B page
> geometry相符，但这里只把外部datasheet作为命令集兼容性旁证；硬约束仍以Beken BK7258
> 文档、official v3.1.1.9 driver和实板验证为准。

---

## 3. N5-D2 — Read-Only Flash Content Dump

板端 probe 读取 flash 起始地址内容：

```
OFF=00000000 W0=2809f700 W1=02000201 W2=020002b5 W3=020002b9
OFF=00100000 W0=ffffffff W1=ffffffff W2=ffffffff W3=ffffffff
```

**解读**：

- `OFF=0x00000000`：flash 可读起始。`W0=0x2809F700` 近似 MSP（`= 0x2809FFFC` 经 CRC 展开
  后的物理视图偏移，`0x2809F700` 在 CRC-expanded 物理空间合理），`W1=0x02000201` 近似
  Reset_Handler（`0x02010xxx` 区间），后续 word 为向量表条目。bootloader + app 向量表内容
  可读。
- `OFF=0x00100000`：candidate data partition 起始，**全 `0xFF`（erased）**。D4 进一步验证。

---

## 4. N5-D3 — Logical Offset Semantics by Magic Scan

通过扫描 flash 偏移查找 `"BK7236"` app magic：

```
OFF=00000100 initially showed data; scan found N5FS:M MAG=00000100 W0=32374b42 W1=00103633
```

**解读**：

- `W0=0x32374B42` = `"BK72"` LE；`W1=0x00103633` = `"36\0\x10"` LE。前 6 bytes match
  `"BK7236"`（boot magic 格式）。
- Magic 出现在 logical offset `0x100`（= 向量表槽 64/65），与 Tier-1 bootloader 的
  `app header @ logical 0x100` 约定一致。
- **NuttX read path 行为**：flash read offset `0x100` 看到 app magic，表明 NuttX 侧的
  读路径表现为 logical view（对我们的目的而言），不是简单的 CRC-expanded file offset。

**未命中探测**：
- `OFF=0x00011110` 和 `OFF=0x00069888` 直接探测均未找到 magic（这些偏移不在向量表槽 64/65
  对应的 logical 视图范围内）。

---

## 5. N5-D4 — Candidate Data Partition Emptiness Scan

对 candidate data partition 前 16 KB 做 4 个 4KB-aligned 采样：

```
OFF=00100000 W0=ffffffff W1=ffffffff W2=ffffffff W3=ffffffff
OFF=00101000 W0=ffffffff W1=ffffffff W2=ffffffff W3=ffffffff
OFF=00102000 W0=ffffffff W1=ffffffff W2=ffffffff W3=ffffffff
OFF=00103000 W0=ffffffff W1=ffffffff W2=ffffffff W3=ffffffff
```

**解读**：candidate data partition 前 16 KB 全 `0xFF`（erased state），确认该区域当前无有效数据。

---

## 6. 安全 Candidate 汇总

| 属性 | 值 |
|---|---|
| Candidate 范围 | `0x00100000..0x001FFFFF`（logical） |
| 大小 | 1 MB |
| Alignment | 4 KB（sector）/ 64 KB（block） |
| 当前 image 占用 | `0x2837A`（≈ 163 KB），candidate 起始距 image end 约 845 KB |
| Emptiness | 前 16 KB 全 `0xFF`（board-observed） |
| Flash ID | `0xC86517`（BK7258集成8 MB Flash接口；与GD25WQ64E身份兼容） |
| Erase unit | 4 KB sector / 64 KB block |
| Page program | 256 B |
| **D5 destructive 验证** | ✅ board-verified（erase/write/read-back/re-erase） |

> Candidate 区域 `0x00100000..0x001FFFFF` 满足：远在当前 app image 之外、4KB/64KB 对齐、
> 已确认 erased（至少前 16 KB），**且 D5 destructive probe 已 board-verified**（raw flash
> erase/write/read-back/re-erase 成功）。
>
> **⚠️ 不能标 filesystem board-verified**：MTD 层未启用，LittleFS/SmartFS 未挂载。

---

## 7. 状态边界

| 项 | 状态 |
|---|---|
| Flash ID/geometry 读取 | ✅ board-observed（N5-D1） |
| Layout candidate 观察 | ✅ board-observed（N5-D0） |
| App magic scan | ✅ board-observed（N5-D3） |
| Candidate data partition emptiness（前 16 KB） | ✅ board-observed（N5-D4） |
| Flash erase / write / read-back / re-erase | ✅ **board-verified**（N5-D5） |
| MTD 层 | ❌ **未启用** |
| 文件系统（LittleFS / SmartFS） | ❌ **未挂载** |
| 文件系统 board-verified | ❌ **不能标 filesystem board-verified** |

**N5 当前完成 read-only flash/layout preparation + raw flash erase/write/read-back/re-erase；
但未启用 MTD/mount，不能标 filesystem board-verified。**

---

## 8. 风险边界

- **Flash ID 与 SDK 分区表可能不一致**：N5-D1 的 JEDEC ID `0xC86517` 为 board-observed；
  SDK 分区表定义（如有）可能对同一地址段有不同分区边界。使用 SDK 分区表时需交叉验证。
- **Layout candidate 来源**：N5-D0 的 layout 输出来自 NSH probe 命令，非 SDK `partition_get_info()`
  返回值。两者可能对同一物理 flash 有不同的分区方案。
- **Logical offset 语义**：N5-D3 表明 NuttX read path 对我们的 flash 读取表现为 logical view；
  N5-D5 已验证 `0x00100000` write path 使用同一 logical candidate。但这仍不等于完整分区、
  MTD 或 filesystem 语义。
- **Data partition 覆盖范围有限**：candidate 区域 1 MB；D4 只读采样前 16 KB，D5 只对首个
  4 KB sector 做 destructive erase/write/read-back/re-erase 验证。完整 1 MB 仍未全量扫描或格式化。

---

## 9. N5-D5 — Destructive Erase/Write/Read-Back/Re-Erase Board-Verified

**授权**：用户显式授权 D5 destructive probe（flash 损坏风险极低——candidate 区远在 app 外）。

**范围**：erase/re-erase 范围是 `0x00100000..0x00100FFF`（第一个 4 KB sector）；
write/read-back pattern 范围是 `0x00100000..0x001000FF`。

**前置条件验证**：当前 `all-app.bin`（`0x2837A`）在 bootloader + app slot 内
（`0x00000000..0x000FFFFF`），D5 操作的 `0x00100000` 不覆盖 boot/app 区。

**D5 执行结果**（板端 probe 输出摘要）：

```
N5FS:D5
PRE OFF=00100000 W0=ffffffff W1=ffffffff W2=ffffffff W3=ffffffff
PB SR0=1c SR1=00 ...
PU SR0=00 SR1=00 ...
ERA OK
ERD OFF=00100000 ... ffffffff
WO OFF=00100000 / 00100020 / 00100040 / 00100060 / 00100080 / 001000a0 / 001000c0 / 001000e0
WT OFF=001000f0
WE SR0=02, WP SR0=00
WRD0 OFF=00100000 all deadbeef
WRDE OFF=001000e0 all deadbeef
WRDF OFF=001000f0 all deadbeef
RER OK
FIN OFF=00100000 all ffffffff
PR SR0=1c SR1=00
N5FS:D5 OK
```

**步骤与结果**：
1. **PRE（读取前状态）**：`OFF=0x00100000` 全 `0xFFFFFFFF`（erased），确认起始状态。
2. **PB/PU（protect bit 操作）**：SR0/SR1 读取并清除保护，确保可写。
3. **ERA（erase）**：4 KB sector erase 成功。
4. **ERD（erase 后 read-back）**：全 `0xFFFFFFFF`，验证 erase 完成。
5. **WO（write offset 采样）**：写入 8 个 offset（`0x00100000` 至 `0x001000E0`，步长 `0x20`）。
6. **WT（write tail）**：`OFF=001000f0`。
7. **WE/WP（status observation）**：WE = write enable observed `SR0=02`；WP = post-program status `SR0=00`，说明 WEL 被消耗且 protection remained cleared during test。
8. **WRD0/WRDE/WRDF（write read-back）**：所有写入 offset 读回 `0xDEADBEEF`，验证写入一致。
9. **RER（re-erase）**：re-erase 成功。
10. **FIN（最终 read-back）**：`OFF=0x00100000` 全 `0xFFFFFFFF`，验证恢复 erased state。
11. **PR（protect restore）**：SR0=1c SR1=00，恢复保护位。

**D5 probe 关闭验证**：D5 probe 关闭后的普通版本启动输出不再包含 `N5FS:D5`；
`N5FS:D` 采样 `0x00100000`/`0x00101000`/`0x00102000`/`0x00103000` 全 `0xFFFFFFFF`，
确认 D5 probe 已正确关闭且 flash 状态已恢复。

**状态边界**：
- ✅ **board-verified**：raw flash erase / write / read-back / re-erase
- ❌ **不能标 filesystem board-verified**：MTD 层未启用，LittleFS/SmartFS 未挂载

---

## 10. N5-D6 — MTD Lower-Half (Read + Write Board-Verified)

**范围**：把 Stage N5 已验证的 data partition 包成 NuttX MTD lower-half。read/erase/bwrite
均已在板端验证；erase/bwrite 用方案 A（每次操作临时清/恢复 SR0 保护）。

**新增文件**：

- `board/bk7258_t5ai/chip/cp/bk7258_flash_mtd.h` — 暴露 `bk7258_flash_mtd_initialize()`
  及（gated）`bk7258_flash_mtd_selftest()`。
- `board/bk7258_t5ai/chip/cp/bk7258_flash_mtd.c` — `struct mtd_dev_s` 实现。

**MTD 接口**：

| 方法 | 状态 |
|---|---|
| `bread` | ✅ board-verified（blocksize = erasesize = 4096，256 blocks，**32B 粒度、0x20 对齐**读） |
| `ioctl(MTDIOC_GEOMETRY)` | ✅ board-verified |
| `ioctl(MTDIOC_ERASESTATE)` | ✅ 实现（返回 `0xff`） |
| `erase` | ✅ board-verified（方案 A：入口清 SR0 → 每 sector `swop(addr,SE)` → 出口恢复 SR0） |
| `bwrite` | ✅ board-verified（方案 A：清 SR0 → 每 32B `WREN+8words+PP` → 恢复 SR0；由 fs 层先 erase） |
| `/dev` 节点 / fs 挂载 | ❌ 不注册、不挂载（待授权） |

**关键设计（方案 A SR0 保护）**：flash 默认 `SR0=0x1c` 块保护覆盖 boot/app 区。erase/bwrite
在调用入口 `bk7258_flash_unprotect()` 清保护位、出口 `bk7258_flash_restore()` 恢复，使 boot/app
区在 op 窗口外仍受硬件保护。

**板端自测证据**（`CONFIG_BK7258_FLASH_MTD_SELFTEST`，测后已关回）：

```
N5FS:MTDW SR0b=0000001c
ERA OK
WR OK
CMP OK
RER OK
FIN OK
SR0a=0000001c
N5FS:MTDW OK
W
```

- `SR0b=0000001c` → 操作前默认保护值。
- `ERA/WR/CMP/RER/FIN OK` → erase→write 位置相关 pattern（`0xDEAD0000|i`）→ bread 逐字
  比对 → re-erase → 全 `0xff`，全通过。
- `SR0a=0000001c` → **方案 A 恢复成功**，操作前后保护位一致。
- block 0 测后恢复为 erased state。

**自测发现并修复的 bug**：初版 `bread` 用 16B 粒度（`read16` 读 4 words，步进 16），在
非 0x20 对齐地址发 READ 导致返回下一 0x20 边界的数据（`CMPBAD OFF=0x10 EXP=dead0004
GOT=dead0008`）。根因：控制器 READ 是 32 字节突发，必须 0x20 对齐。修复为 `read32`
（8 words，步进 32，与 SDK `flash_read_data` 一致）。write 侧本就 32B 对齐 PP，未动。

**init 证据行（板端实测）**：

```
N5FS:MTD FID=00c86517 SZ=00100000 ESZ=00001000 D0=ffffffff D1=ffffffff
M
```

**编译验证**：

- `CONFIG_BK7258_FLASH_MTD=y` 重编通过，无 error/warning。
- 自测关回后 `nuttx.bin = 92196 B`，`all-app.bin = 167620 B = 0x28EC4`，仍 `< 0x00100000`。

**状态**：

- ✅ read + erase + bwrite：board-verified（方案 A SR0 清/恢复）。
- ✅ 见 §11 N5-D7：filesystem board-verified。

---

## 11. N5-D7 — LittleFS Filesystem (Board-Verified)

**范围**：在已验证的 data-partition MTD 上注册 ftl 块设备 → 挂载 LittleFS（autoformat）→
probe 文件持久化自检（首次创建 / 重启读回）。

**新增配置**：

- `CONFIG_BK7258_FLASH_LITTLEFS`（chip/Kconfig，默认 n，`select FS_LITTLEFS`）。
- bringup 新增 `bk7258_fs_probe()`：`ftl_initialize(0, mtd)` 注册 `/dev/mtdblock0` →
  `mount("/dev/mtdblock0","/data","littlefs",0,"autoformat")` → probe 文件读/建。

**关键配置修复**：LittleFS 默认 `*_SIZE_FACTOR=4`（为 512B 扇区设备设计），在 4KB block 上算出
`read_size=16384 > block_size=4096`，违反 littlefs 约束导致 `lfs_format` 失败（首次 mount 报错）。
修复为 `READ/PROGRAM/CACHE_SIZE_FACTOR=1`（block 保持 1）→ read=prog=cache=block=erasesize=4096，
全一致。`savedefconfig` 精简为只显式写 `PROGRAM_SIZE_FACTOR=1`，其余派生为 1。

**板端证据（两启动闭环）**：

首次启动（format + 创建 probe）：
```
... M L C
nsh> cat /data/probe.txt
BK7258LFS-OK
```

重启后（挂载 + 读回 probe = 持久化）：
```
... M L R
nsh> cat /data/probe.txt
BK7258LFS-OK
```

落盘证据：`N5FS:D` 在 `0x00100000` 读到 `W2=7474696c ("litt") W3=7366656c ("elfs")` =
磁盘上的 LittleFS superblock magic，确认 fs 落盘。

**编译验证**：`CONFIG_BK7258_FLASH_LITTLEFS=y` distclean 重编通过，无 error/warning；
`nuttx.bin = 115416 B`（含 littlefs），`all-app.bin = 192270 B = 0x2EF0E`，仍 `< 0x00100000`，
boot/app 区不受影响。

**状态**：✅ **filesystem board-verified**（LittleFS format/mount/write/reboot-persistence 全链路板端通过）。
`/dev/mtdblock0` + `/data` 正常；boot/app 区未被触碰。

---

## 12. Worklog 元数据

| 项 | 值 |
|---|---|
| Stage | N5 |
| Substage | D0（layout）、D1（ID/geometry）、D2（content dump）、D3（magic scan）、D4（emptiness scan）、**D5（raw erase/write board-verified）**、**D6（MTD lower-half read/erase/bwrite board-verified，方案 A SR0）**、**D7（LittleFS filesystem board-verified）** |
| 板端验证日期 | 2026-07-19（D0–D7） |
| Artifact | `$FW/all-app.bin`：D7 版 192270 B = `0x2EF0E`（< `0x100000`） |
| 文件系统状态 | **board-verified**（LittleFS，data 分区 1 MiB @ `0x00100000`，挂载 `/data`） |
