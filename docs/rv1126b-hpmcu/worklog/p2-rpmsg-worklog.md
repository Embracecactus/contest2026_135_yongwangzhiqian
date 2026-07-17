# P2 RPMsg 工作日志

## 概述

本文档记录 openvela 2026 竞赛 RV1126B P2 RPMsg/RPTun 阶段从静态设计到 Route A 板端验证通过的完整时间线。每步记录关键决策、产物 hash 和验证结论。

## 时间线

### 2026-07-16：P2-A 静态设计与代码审查

**决策**：基于 P1 NSH baseline，在 `$CONTEST/board/contest_board/` 下实现 mailbox 驱动和 RPTUN 对接。

**审查范围**：
- mailbox 驱动（`rv1126b_mailbox.c`/`.h`、`hardware/rv1126b_mailbox.h`）
- RPTUN 对接层（`rv1126b_rptun.c`）
- 链接脚本（`ld.script`）
- Kconfig、Make.defs、defconfig
- bringup 集成（`rv1126b_bringup.c`、`rv1126b_evb.h`）

**关键设计决策**：
1. Private static resource table（仅含 VDEV，num=1，notifyid=2）
2. 固定 vring 在 SHM 基址 0x48c3c000，128 KiB NOLOAD MEMORY region
3. MBOX7 A2B (RX Linux→HPMCU)、MBOX4 B2A (TX vqid0)、MBOX7 B2A (TX vqid1)
4. Wire protocol CMD=0x03, DATA=0x524d5347，两次 32-bit MMIO 写入
5. V2.0 busy merge：busy 时返回 OK 不重发
6. ISR 与手动 drain 共用，callback 在锁外执行
7. CONFIG_DEV_SIMPLE_ADDRENV identity mapping
8. CONFIG_RPTUN_AUTO_RESET_DISABLE=y

**审查结论**：设计批准，进入实现阶段。

### 2026-07-16：P2-A 源码实现

**新建文件**（untracked）：
- `board/contest_board/chip/hardware/rv1126b_mailbox.h`
- `board/contest_board/chip/rv1126b_mailbox.c`
- `board/contest_board/chip/rv1126b_mailbox.h`
- `board/contest_board/chip/rv1126b_rptun.c`

**修改文件**（tracked modified）：
- `board/contest_board/chip/Kconfig`（新增 SHM base/size、RPTUN select 等）
- `board/contest_board/chip/Make.defs`（新增 mailbox.o、rptun.o）
- `board/contest_board/configs/nsh/defconfig`（新增 P2-A 相关选项）
- `board/contest_board/scripts/ld.script`（新增 linux_rpmsg MEMORY region、NOLOAD section、ASSERT）
- `board/contest_board/src/rv1126b_bringup.c`（新增 rv1126b_rptun_init 调用）
- `board/contest_board/src/rv1126b_evb.h`（新增 RPTUN 寄存器基址和 IRQ 定义）

### 2026-07-16：Classic Make 构建（初版，构建时间 2026-07-16 约 19:30 CST）

**构建产物**（初版 P2-A，含 rpmsgchar）：
- nuttx: 228568 B, SHA-256: 68d7cb2706b9f1cf1e827c50d0768f0cf883b7e48ed747b931453155cb5775a0
- nuttx.bin: 129900 B, SHA-256: babbbe52bd438842a9cd0fb867c2ffe269d130d5f7bd6f70db5b1f8f51e31e33
- nuttx.map: 942007 B, SHA-256: 5fbbdc3c44591f8a21ccb2bc756c0b8d1a0c5a44ccc0a691a2b5f5471830c5d3

**size**：text=128628, data=1272, bss=6564, dec=136464

**注意**：此初版构建后来被 Route A 最终构建（2026-07-17 00:42）取代。初版 hash 仅保留作历史参考；当前有效产物见下方 Route A 构建。

### 2026-07-16：SDK 打包差异排查

**问题**：最初不清楚正确的 AMP 打包流程，存在多条历史路径混淆。

**排查结论**：
- 正确打包链：`$OUT/rtt.bin` target → `$OUT/amp.its` → `$FW/amp.img`
- 正确 mkimage 来自 SDK toolchain，非 `$SDK/hal/tools/mkimage`
- `$OUT/rtt.bin` 是 symlink → `rtos/bsp/rockchip/rv1126b-mcu/Image/rttmcu.bin`
- 历史路径 `$SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/rtt.bin`、`Image/nuttx_amp.img` 不得混用

### 2026-07-17 00:42 CST：Route A 最终构建

**背景**：Route A 最终改用标准 `apps/examples/rpmsgchar`（发现 hello_app Kconfig 接线缺口后）。

**构建命令**：
```bash
cd $WORKSPACE
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh distclean
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
```

**构建产物**：
- nuttx: 241864 B, SHA-256: 5ab48cf8dec520389d008aef6301c4cf6c48fe48f8f716e01ef6091aa05b8001
- nuttx.bin: 139668 B, SHA-256: 14c1c40921c1b733464d40538d559b4ed27e54053919cef854acf20919f779c1
- nuttx.map: 997032 B, SHA-256: 6b0251c72ebfd2c70927ef72fe515544089e4122c10d61eded2c5f7b23685961

**size**：text=138380, data=1288, bss=6568, dec=146236

**内存布局**：
- RAM: 146236 / 237568 = 61.56%
- heap: 0x48c25b40 .. 0x48c3c000 (~91 KiB)
- RPMsg SHM: 0x48c3c000 .. 0x48c5c000 (128 KiB, NOLOAD)

**关键 config**：
- CONFIG_RPTUN=y, CONFIG_RPTUN_AUTO_RESET_DISABLE=y
- CONFIG_DEV_SIMPLE_ADDRENV=y
- CONFIG_RPMSG=y, CONFIG_RPMSG_VIRTIO=y, CONFIG_RPMSG_CHAR=y
- CONFIG_BUILTIN=y, CONFIG_NSH_BUILTIN_APPS=y
- CONFIG_EXAMPLES_RPMSGCHAR=y

**打包产物**：
- $FW/amp.img: 144384 B, SHA-256: 073a58da051c3a6a8565aee5927f356348fcde0b3493850184a78a4d66ddd137
- $OUT/update/Image/update.img: 1451455050 B, SHA-256: d1cfa165ad84d9de24f7af341ab72240301394e4284ca2fcd4c11a2914393b17
- $OUT/rtt.bin target (rttmcu.bin) 与 nuttx.bin 字节全等

### 2026-07-17：刷机与 Board Handshake 验证

**刷机**：完整 `$OUT/update/Image/update.img` 升级。

**Linux→HPMCU Handshake 验证**：
- `/dev/rptun/ap` 存在
- `/dev/rpmsg/ap` 存在
- `rpmsg-ap-0` worker Waiting Semaphore
- MBOX7 A2B_INTEN=0x101, A2B_STATUS=0, CMD=0x03, DATA=0x524d5347
- MBOX4/7 B2A_INTEN=0x101

**结论**：Linux handshake 被 HPMCU 消费，RPTUN/OpenAMP 本地设备创建成功。

### 2026-07-17：Route A 板端验证

**NuttX 侧**：
```text
rpmsgchar -c /dev/rpmsg/ap -n rpmsg-demo
rpmsgchar [5:100]
Start the echo test
```
随后在首个 `read` 阻塞（Linux 无 echo client，预期）。

**Linux 侧**：
- IRQ `20d00000.mailbox` (IRQ line 93, GICv2 141 Level) 计数 0→1
- `20d30000.mailbox` 保持 0
- dmesg: `virtio_rpmsg_bus virtio0: creating channel rpmsg-demo addr 0x400`
- `/sys/bus/rpmsg/devices/virtio0.rpmsg-demo.-1.1024` 新增

**完整路径验证**：
NuttX rpmsgchar 创建 ept → NS_CREATE → TX vring0/notifyid0 → rp_notify(0) →
MBOX4 B2A doorbell → Linux GIC IRQ 93 → virtio rpmsg bus 解析 →
NS channel "rpmsg-demo" addr 0x400 创建 → sysfs 设备节点出现。

### 2026-07-17：打包结论修正

- 完整 update.img 升级已验证可靠。
- 用户确认单独 amp.img 也可成功加载（uname 新时间、/dev/rptun/ap 成功）。
- 单独 amp.img 可用于迭代，但须用 uname + /dev/rptun/ap 验证加载。

## 当前里程碑

**Route A 完成**：
- 双向 mailbox doorbell 板端通过
- Linux handshake 通过
- RPTUN/RPMsg 建链通过
- NS channel 枚举通过
- 基础 NS announce 路径共享内存/cache 可见性通过

**下一里程碑 Route B**：
- Linux echo client 实现
- 双向 payload echo 验证
- 多种 payload 长度测试

## 发现的问题

1. **Contest app 构建缺口**：`app/hello_app` Kconfig 未被 `packages/Kconfig` source，
   hello_app 不进 ELF。Route A 改用标准 `apps/examples/rpmsgchar`。
   这是后续需单独处理的问题，不属于 P2-A mailbox 修复范围。

2. **未修复 warnings**：MSTATUS_MIE redefined、ELF RWX LOAD segment。

3. **手动 reset 不支持**：CONFIG_RPTUN_AUTO_RESET_DISABLE=y 下无 RPTUNIOC_RESET 实现。

## 关键文件

| 文件 | 用途 |
|------|------|
| `board/contest_board/chip/rv1126b_mailbox.c` | RV1126B mailbox 驱动 |
| `board/contest_board/chip/rv1126b_mailbox.h` | Mailbox 内部头文件 |
| `board/contest_board/chip/hardware/rv1126b_mailbox.h` | Mailbox 寄存器定义 |
| `board/contest_board/chip/rv1126b_rptun.c` | RPTUN 对接层（resource table、vring、bringup） |
| `board/contest_board/scripts/ld.script` | 链接脚本（linux_rpmsg NOLOAD MEMORY region） |
| `board/contest_board/configs/nsh/defconfig` | NSH defconfig（含 P2-A 选项） |
| `board/contest_board/src/rv1126b_bringup.c` | Board bringup（rv1126b_rptun_init 调用） |
| `docs/rv1126b-hpmcu/next-stage-prompts/p2-rpmsg-next-stage-prompt.md` | Route A 恢复提示词（含全部证据） |
