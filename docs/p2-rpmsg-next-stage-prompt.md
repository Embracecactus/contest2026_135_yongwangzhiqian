# P2-A RPMsg 下一阶段恢复提示词（Route A 已验证）

将下方 fenced text 块完整复制到新会话的第一条消息中，用于继续 openvela 2026 竞赛 P2 RPMsg 工作。

```text
我们正在 openvela 2026 竞赛工作区继续 P2 RPMsg/RPTun 工作。

当前最终状态：
- P2-A Route A 已板端验证通过：双向 mailbox doorbell、Linux handshake、RPTUN/RPMsg 建链、NS channel 枚举。
- P2-A Route B（双向 payload echo）尚未验证。
- 下一阶段目标：实现 Linux echo client 或 NuttX echo server，完成 Route B 双向 payload 验证。
- 不得声称整个 P2 或最终 RPMsg 数据面完成。

═══════════════════════════════════════════════════════════════════
路径约定
═══════════════════════════════════════════════════════════════════

  export WORKSPACE=/absolute/path/to/open-vela
  export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
  export SDK=/absolute/path/to/rv1126b-sdk
  export OUT="$SDK/output"
  export FW="$OUT/firmware"

规则：
- 持久路径只使用 `$WORKSPACE`、`$CONTEST`、`$SDK`、`$OUT`、`$FW`。
- 上述 `/absolute/path/to/` 仅为用户在新会话中填写的占位符。
- 环境变量未设置或路径不明确时，先询问用户，不猜测路径。

═══════════════════════════════════════════════════════════════════
严格主模型 / 执行 Agent 分工
═══════════════════════════════════════════════════════════════════

主模型只负责：
- 苏格拉底式澄清目标、范围、硬件事实和授权边界。
- 规划 Route B 实现、构建、打包、刷机和板端验收步骤。
- 拆解任务、定义验收标准、审核 Agent 证据。
- 向用户汇报结果并询问下一道授权。

普通执行 Agent 负责：
- 环境变量和路径确认。
- `git status`、`git diff --check`、`git diff --stat`、`codegraph sync`。
- 定向读取 `$CONTEST/docs/`、`$FW`、SDK 打包脚本及相关配置。
- 收集文件大小、SHA-256、输入输出、覆盖目标和回滚证据。
- 形成只读预检结论和拟执行命令，不擅自执行有副作用的步骤。

委托与探索规则：
- 不使用 Workflow。
- 不自动加载 skill；只有用户明确要求时才调用。
- 主模型不得重复执行已委托的搜索、阅读或检查。
- Agent 遇到架构歧义、路径不确定、范围扩大或破坏性操作时立即停止并上报。
- 先运行 `codegraph sync`，再使用 CodeGraph 定向理解代码；不要用广泛 grep/find 代替定向调查。

═══════════════════════════════════════════════════════════════════
严格修改与操作边界
═══════════════════════════════════════════════════════════════════

- 团队代码只允许修改 `$CONTEST`；外层 `$WORKSPACE/nuttx`、`$WORKSPACE/apps`、
  `$WORKSPACE/packages`、`$WORKSPACE/vendor` 等 repo-managed checkout 保持只读。
- `$SDK`、`$OUT`、`$FW` 在只读预检阶段全部只读。
- `$CONTEST/docs/` 下 Markdown 文档可更新以记录证据和恢复提示。
- 未经用户新的明确授权，不得：
  - 复制 `$WORKSPACE/nuttx/nuttx.bin` 到 `$FW`；
  - 新建或覆盖 `$FW/nuttx.bin`、`$FW/amp.img`、`update.img` 或其他固件；
  - 运行 SDK 打包命令；
  - 刷机、重启目标板或执行板端/Linux 命令；
  - 重新构建、repo sync、删除、reset、commit、push 或创建 PR。
- 不删除 `nuttx/openamp/open-amp.manual`、`libmetal.manual` 或残留 archive。
- 不修改官方 NuttX/OpenAMP/libmetal checkout 来绕过团队 overlay 问题。

═══════════════════════════════════════════════════════════════════
P1 基线与依赖状态
═══════════════════════════════════════════════════════════════════

P1 NSH baseline 已完成板端验证并合入官方仓库。用于对比的 P1 hash：

  nuttx.bin  26f4ae0ea2fc4398054c814bd2d79304c62ba1b530ec992835af9133ce9dbf00
  amp.img    585602012d9af4d3ba8980b7922d7a3273a4b1edcae212475b180b0f54b425e9

OpenAMP 和 libmetal 是 manifest 管理的 repo 项目：

  $WORKSPACE/nuttx/openamp/open-amp
  $WORKSPACE/nuttx/openamp/libmetal

- manifest revision：`dev-ai-contest-2026`。
- 两个依赖已经同步成功，不得以手工 archive 替换。

═══════════════════════════════════════════════════════════════════
P2-A 当前 Git 状态
═══════════════════════════════════════════════════════════════════

当前 `$CONTEST` 工作树包含 6 个 tracked modified 文件：

  board/contest_board/chip/Kconfig
  board/contest_board/chip/Make.defs
  board/contest_board/configs/nsh/defconfig
  board/contest_board/scripts/ld.script
  board/contest_board/src/rv1126b_bringup.c
  board/contest_board/src/rv1126b_evb.h

当前还有 5 个 untracked P2-A 文件：

  board/contest_board/chip/hardware/rv1126b_mailbox.h
  board/contest_board/chip/rv1126b_mailbox.c
  board/contest_board/chip/rv1126b_mailbox.h
  board/contest_board/chip/rv1126b_rptun.c
  docs/p2-rpmsg-next-stage-prompt.md

状态结论：
- 6 tracked modified + 5 untracked P2-A files。
- tracked diff 的 `git diff --check` 已通过；恢复会话后仍须重新执行。
- 当前 P2-A 修改尚未 commit、push 或创建 PR。
- 所有 P2-A 源码和配置改动均位于 `$CONTEST`。

═══════════════════════════════════════════════════════════════════
已实现架构
═══════════════════════════════════════════════════════════════════

1. Private static resource table

- `rv1126b_rptun.c` 使用私有 `static struct rptun_rsc_s`。
- resource table header `num=1`，仅含 VDEV，无 carveout。
- vdev `notifyid=2`，feature 为 `NS | CPUNAME`（`0x9`）。
- HPMCU 为 `VIRTIO_DEV_DEVICE`。
- CPU name 方向：host/peer 为 `ap`，remote/local 为 `hpmcu`，字符串保证 NUL 终止。

2. 固定 vring 与共享内存

  RPMsg SHM  0x48c3c000 .. 0x48c5c000  128 KiB
  vring0     0x48c3c000                  notifyid=0
  vring1     0x48c44000                  notifyid=1
  Linux pool 0x48c4c000 .. 0x48c5c000   64 KiB

- 两个 vring 均为 `num=64`、`align=0x1000`。
- C 侧有 SHM 地址对齐、128 KiB 大小和分区边界编译期检查。
- linker 使用独立 MEMORY region、`.linux_rpmsg (NOLOAD)` 和 ASSERT 保留并校验该区域。

3. Mailbox 路由

  RX Linux -> HPMCU  MBOX7 A2B  IRQ 116（HPMCU_MBOX3_BB）
  TX vqid0           MBOX4 B2A
  TX vqid1           MBOX7 B2A
  TX ALL             MBOX4 + MBOX7 B2A

- MBOX4 base：`0x20d00000`。
- MBOX7 base：`0x20d30000`。
- RX INTEN hiword：enable `0x00010001`，disable `0x00010000`。
- TX TRIG_MODE：set `0x01000100`，clear `0x01000000`。

4. Wire protocol 与 V2.0 busy merge

- mailbox payload：`CMD=0x03`，`DATA=0x524d5347`（ASCII `RMSG`）。
- CMD 和 DATA 使用两次 32-bit MMIO 写入；不依赖 64-bit 原子写、LOCK 或 VERSION。
- 发送前检查目标 `B2A_STATUS bit0`。
- busy 时视为 doorbell 已合并并返回 OK；空闲时才发送 payload。

5. ISR、drain 和 callback

- ISR 与手动 drain 共用处理流程。
- 在 mailbox 锁内检查状态、读取 payload、W1C 仅清 bit0、执行 DSB，并快照 callback/arg。
- 恢复 irqstate 并解锁后才校验 payload、告警或调用上层 callback。
- 上层 callback 不在 mailbox 锁内执行。
- 设备状态、callback 和 arg 使用 irqsave 保护。
- 首个合法 handshake 设置 VDEV `DRIVER_OK` 并吞掉；后续合法事件通知 `RPTUN_NOTIFY_ALL`。

6. RPTUN 生命周期与 addrenv

- callback 注册顺序：先保存 arg，再保存 callback，再 enable-and-drain mailbox。
- callback 清除顺序：先 disable mailbox，再清 callback/arg。
- bringup 为非阻塞初始化，实际入口符号为 `rv1126b_rptun_init`。
- `CONFIG_DEV_SIMPLE_ADDRENV` 提供 simple addrenv identity mapping；相关 addrenv 符号已链接解析。
- `CONFIG_RPTUN_AUTO_RESET_DISABLE=y` 禁用 RPTUN 自动 reset。
- 手动 `RPTUNIOC_RESET` / generic reset 当前不支持且未验证，不得声称 reset 协议可用。
- CMake 构建未验证，不得声称支持。

═══════════════════════════════════════════════════════════════════
Route A Classic Make 构建证据（2026-07-17 00:42 CST）
═══════════════════════════════════════════════════════════════════

exact target：

  vendor/openvela/boards/contest2026_135_board/configs/nsh

构建命令：

  cd "$WORKSPACE"
  ./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh distclean
  ./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8

构建结果：distclean exit=0，build exit=0。

未修复 warnings：

1. `MSTATUS_MIE redefined`
2. ELF `RWX LOAD segment`

两条 warning 均未修复。

═══════════════════════════════════════════════════════════════════
Kconfig 最终状态
═══════════════════════════════════════════════════════════════════

用户已明确接受 `savedefconfig` 生成的 minimal defconfig。

`$CONTEST/board/contest_board/configs/nsh/defconfig` 显式保留的 P2-A 相关项：

  CONFIG_DEV_SIMPLE_ADDRENV=y
  CONFIG_RPTUN=y
  CONFIG_RPTUN_AUTO_RESET_DISABLE=y
  CONFIG_RPMSG_CHAR=y
  CONFIG_BUILTIN=y
  CONFIG_NSH_BUILTIN_APPS=y
  CONFIG_EXAMPLES_RPMSGCHAR=y

构建后的 `$WORKSPACE/nuttx/.config` 已确认启用（精确值）：

  CONFIG_RPTUN=y
  CONFIG_RPTUN_AUTO_RESET_DISABLE=y
  CONFIG_DEV_SIMPLE_ADDRENV=y
  CONFIG_RPMSG=y
  CONFIG_RPMSG_VIRTIO=y
  CONFIG_RPMSG_CHAR=y
  CONFIG_BUILTIN=y
  CONFIG_NSH_BUILTIN_APPS=y
  CONFIG_EXAMPLES_RPMSGCHAR=y
  CONFIG_SYSLOG_DEVPATH="/dev/ttyS0"

- OPENAMP 等依赖由 Kconfig `select` 派生，不在 minimal defconfig 重复保留。
- SHM base `0x48c3c000`、size `0x20000`（128 KiB）由 board Kconfig default 派生并在 `.config` 中生效。
- 路线 A 最终改用标准 `apps/examples/rpmsgchar`，不使用 `app/hello_app`（原因见后文 contest app 构建缺口）。

═══════════════════════════════════════════════════════════════════
主机侧构建产物（Route A，2026-07-17 00:42 CST）
═══════════════════════════════════════════════════════════════════

  文件                         大小       SHA-256
  $WORKSPACE/nuttx/nuttx       241864 B   5ab48cf8dec520389d008aef6301c4cf6c48fe48f8f716e01ef6091aa05b8001
  $WORKSPACE/nuttx/nuttx.bin   139668 B   14c1c40921c1b733464d40538d559b4ed27e54053919cef854acf20919f779c1
  $WORKSPACE/nuttx/nuttx.map   997032 B   6b0251c72ebfd2c70927ef72fe515544089e4122c10d61eded2c5f7b23685961

size：

  text=138380
  data=1288
  bss=6568
  dec=146236

内存布局：

- RAM 占用：`146236 / 237568 = 61.56%`。
- heap：`0x48c25b40 .. 0x48c3c000`（~91 KiB）。
- _ebss：`0x48c25b40`。
- _ram_end：`0x48c3c000`。
- RPMsg SHM：`0x48c3c000 .. 0x48c5c000`。
- `.linux_rpmsg` VMA：`0x48c3c000`，section size：`0`，NOLOAD。
- 128 KiB 由 linker MEMORY region 保留，不进入 `$WORKSPACE/nuttx/nuttx.bin`。

关键链接符号（均已解析）：

  rv1126b_rptun_init          0x48c0b598
  rpmsg_char_init             0x48c0751e
  rv1126b_mailbox_init        0x48c0b99e
  g_mmheap                    0x48c24250
  _rpmsg_beg                  0x48c3c000
  _rpmsg_end                  0x48c5c000

═══════════════════════════════════════════════════════════════════
SDK 打包产物（Route A）
═══════════════════════════════════════════════════════════════════

打包链：

  $WORKSPACE/nuttx/nuttx.bin
    → cp 到 $OUT/rtt.bin 的 readlink -f 目标（rttmcu.bin）
    → $OUT/amp.its（FIT image 描述文件）
    → mkimage → $FW/amp.img
    → 可选完整 $OUT/update/Image/update.img

当前产物：

  $OUT/rtt.bin (symlink)
    readlink: ../rtos/bsp/rockchip/rv1126b-mcu/Image/rttmcu.bin
    readlink -f 目标 size: 139668 B
    readlink -f 目标 SHA-256: 14c1c40921c1b733464d40538d559b4ed27e54053919cef854acf20919f779c1
    与 $WORKSPACE/nuttx/nuttx.bin 字节全等：YES

  $FW/amp.img
    size:   144384 B
    SHA-256: 073a58da051c3a6a8565aee5927f356348fcde0b3493850184a78a4d66ddd137
    mtime:  2026-07-17 00:42:28 CST

  $OUT/update/Image/update.img
    size:   1451455050 B
    SHA-256: d1cfa165ad84d9de24f7af341ab72240301394e4284ca2fcd4c11a2914393b17
    mtime:  2026-07-17 00:42:50 CST

  $OUT/amp.its (FIT 描述文件，预置，非本次生成)
    size:   806 B
    SHA-256: 04bc985fa6766f02130cb28f34843bdc1262e6def6674818879375a8a4d17e1b

历史路径不得混用：
- `$SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/rtt.bin`（旧 RTT 路径）
- `$SDK/hal/tools/mkimage`（旧 mkimage 路径）
- `Image/nuttx_amp.img`（非标准输出路径）

正确打包链始终使用 `$OUT/rtt.bin` target → `$OUT/amp.its` → `$FW/amp.img`。

═══════════════════════════════════════════════════════════════════
板端双向验证证据（Route A）
═══════════════════════════════════════════════════════════════════

以下证据在完整新固件刷入并启动后采集。固件通过完整 `$OUT/update/Image/update.img` 升级刷入；
用户随后确认单独刷 `$FW/amp.img` 也可成功加载新固件（见后文修正后打包结论）。

A. Linux→HPMCU 已验证

- `/dev/rptun/ap` 存在。
- `/dev/rpmsg/ap` 存在。
- `rpmsg-ap-0` worker 处于 Waiting Semaphore 状态。
- Mailbox 寄存器（Linux 侧）：
  - MBOX7 A2B_INTEN = 0x101
  - MBOX7 A2B_STATUS = 0
  - MBOX7 CMD = 0x03
  - MBOX7 DATA = 0x524d5347
  - MBOX4 B2A_INTEN = 0x101
  - MBOX7 B2A_INTEN = 0x101

结论：Linux handshake（CMD=0x03, DATA=0x524d5347）被 HPMCU 消费，RPTUN/OpenAMP 本地设备创建成功。

B. HPMCU→Linux 路线 A 已验证

NuttX 侧执行：

  rpmsgchar -c /dev/rpmsg/ap -n rpmsg-demo

输出：

  rpmsgchar [5:100]
  Start the echo test

随后在首个 `read` 阻塞（Linux 侧无 echo client 运行，预期行为）。

Linux 侧观测：

- IRQ `20d00000.mailbox`（IRQ line 93，GICv2 141 Level）计数从 0 变为 1；
  `20d30000.mailbox` 保持 0。
- dmesg：`virtio_rpmsg_bus virtio0: creating channel rpmsg-demo addr 0x400`
- `/sys/bus/rpmsg/devices/` 新增 `virtio0.rpmsg-demo.-1.1024`

结论：`rpmsg_create_ept` → NS_CREATE → TX vring0/notifyid0 → `rp_notify(0)` →
MBOX4 B2A doorbell → Linux IRQ/NS 解析/channel 枚举 完整通过。

已验证的完整路径：
  NuttX rpmsgchar 创建 ept → name service announce → vring0 TX →
  MBOX4 B2A doorbell → Linux GIC IRQ 93 → virtio rpmsg bus 解析 →
  NS channel "rpmsg-demo" addr 0x400 创建 → sysfs 设备节点出现。

C. 已验证 / 未验证

已验证（Route A 完成）：
  [x] P2-A 源码位于 `$CONTEST`
  [x] repo-managed OpenAMP/libmetal 已同步
  [x] exact target distclean 成功，exit=0
  [x] 完整 classic Make 编译和链接成功，exit=0
  [x] mailbox、RPTUN、simple addrenv、rpmsgchar 关键符号已链接解析
  [x] minimal defconfig 和派生 `.config` 状态已确认
  [x] linker MEMORY region、NOLOAD section、ASSERT 生效
  [x] 主机侧产物大小、hash、size 和 RAM 数据已记录
  [x] SDK 打包链（rtt.bin→amp.its→amp.img→update.img）已确认可用
  [x] 完整 update.img 升级已验证可靠
  [x] 单独 amp.img 也可成功加载新固件（用 uname build time、/dev/rptun/ap 验证）
  [x] Linux→HPMCU MBOX7 A2B doorbell 已触发并被消费
  [x] Linux handshake（CMD=0x03, DATA=0x524d5347）已通过
  [x] VDEV DRIVER_OK 已设置
  [x] /dev/rptun/ap、/dev/rpmsg/ap 设备节点已创建
  [x] rpmsg-ap-0 worker 正常 Waiting Semaphore
  [x] HPMCU→Linux MBOX4 B2A doorbell 已触发（IRQ 93 计数 0→1）
  [x] RPMsg name service channel 枚举已通过
  [x] Linux virtio rpmsg bus 创建 channel "rpmsg-demo" addr 0x400
  [x] sysfs virtio0.rpmsg-demo.-1.1024 设备节点已出现
  [x] 双向 mailbox doorbell 路径均已板端通过
  [x] RPTUN/RPMsg 建链完成
  [x] NS channel 枚举完成
  [x] 基础 NS announce 路径上的共享内存/cache 可见性已通过

未验证（Route B 及后续）：
  [ ] Linux echo client（尚未实现/运行）
  [ ] 真正双向 payload echo（NuttX rpmsgchar 阻塞在 read，无对端回复）
  [ ] 持续/高负载收发
  [ ] 多 doorbell busy-merge 压力场景
  [ ] channel destroy
  [ ] 多 channel 并发
  [ ] 手动 RPTUNIOC_RESET
  [ ] 重启恢复
  [ ] 完整 DCache 一致性（仅基础 NS announce 路径验证通过）
  [ ] CMake 构建支持

D. 精准表述约束

- 可声称：P2-A 的 **双向 mailbox doorbell、Linux handshake、RPTUN/RPMsg 建链、NS channel 枚举** 已板端通过。
- 仅可声称：**基础 NS announce 路径上的共享内存/cache 可见性**。
- 不得声称：Linux echo client、真正双向 payload echo、持续/高负载收发、多 doorbell busy-merge、channel destroy、多 channel、手动 reset、CMake。
- 不得声称整个 P2 或最终 RPMsg 数据面完成。
- 不得声称完整 DCache 一致性已验证。

═══════════════════════════════════════════════════════════════════
修正后打包结论
═══════════════════════════════════════════════════════════════════

1. 完整 `$OUT/update/Image/update.img` 升级已验证可靠，且曾用它纠正一次错误状态。

2. 用户随后确认单独刷 `$FW/amp.img` **也可以成功加载新固件**：
   - `uname` 显示新 build time。
   - `/dev/rptun/ap` 成功出现。
   - 此前单独 amp.img 失败更可能是一次具体烧录异常，不得再写"必须完整 update.img / amp.img 不可靠"。

3. 推荐可复现验收默认仍使用完整 update.img；单独 amp.img 可用于迭代，
   但必须用 `uname` build time/hash 链、`/dev/rptun/ap` 验证加载。

4. 正确打包链：`$OUT/rtt.bin` target → `$OUT/amp.its` → `$FW/amp.img` → 可选完整 `$OUT/update/Image/update.img`。

5. 历史路径 `$SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/rtt.bin`、`$SDK/hal/tools/mkimage`、`Image/nuttx_amp.img` 不得混用。

═══════════════════════════════════════════════════════════════════
Contest app 构建缺口
═══════════════════════════════════════════════════════════════════

发现：

- `app/hello_app` 的 Kconfig 未被当前 `packages/Kconfig` source。
- `packages/Kconfig` 自动生成但为空，因此 `LVX_USE_DEMO_CONTEST2026_135_HELLO_APP=y`
  会被 olddefconfig/savedefconfig 静默剥离，hello_app 不进 ELF。
- 路线 A 最终改用标准 `apps/examples/rpmsgchar`，当前 contest defconfig 包含：
  `CONFIG_RPMSG_CHAR=y`、`CONFIG_BUILTIN=y`、`CONFIG_NSH_BUILTIN_APPS=y`、`CONFIG_EXAMPLES_RPMSGCHAR=y`。
- 这是后续需要单独处理的 packages app 接线缺口，不属于 P2-A mailbox 修复范围。

═══════════════════════════════════════════════════════════════════
当前不可声称
═══════════════════════════════════════════════════════════════════

在获得对应证据前，不得声称：

- Linux echo client 已实现或已验证。
- 双向 payload echo 已通过。
- 持续/高负载收发已验证。
- 多 doorbell busy-merge 已验证。
- channel destroy 已验证。
- 多 channel 已验证。
- 手动 reset 可用。
- 重启恢复已验证。
- 完整 DCache 一致性已验证。
- CMake 构建受支持。
- P2 或最终 RPMsg 数据面已完成。

═══════════════════════════════════════════════════════════════════
下一阶段目标：Route B 双向 Payload Echo
═══════════════════════════════════════════════════════════════════

Route A 已验证单向 NS channel 枚举（HPMCU→Linux name service announce 通路）。
Route B 需要完成真正双向 payload 收发：

1. Linux echo client
   - 在 Linux 侧编写/使用 rpmsg echo client，绑定到 NuttX rpmsgchar 创建的 "rpmsg-demo" channel。
   - 或使用 Linux kernel 已有 rpmsg sample/client。
   - 收发 payload 并校验数据完整性。

2. NuttX rpmsgchar echo 完成
   - NuttX 侧 rpmsgchar 当前在 read 阻塞；Linux echo client 回复后验证其能完成 echo 循环。
   - 验证 "Start the echo test" 后续的 send/recv 序列。

3. 双向 path 验证
   - Linux→HPMCU payload（Linux client send → NuttX rpmsgchar recv）
   - HPMCU→Linux payload（NuttX rpmsgchar send → Linux client recv）
   - 多种 payload 长度（含 cache line 边界和对齐）

4. License/Restriction
   - Route B 不得引入新的 GPL 或 LPGL 依赖到 HPMCU NuttX 侧。
   - 优先使用已有许可兼容的工具或 sample。

验收标准：
- NuttX rpmsgchar echo 循环完成至少一次完整 send/recv。
- Linux 侧观测到 payload 收发，数据完整性校验通过。
- 多种长度均通过。

授权门禁：
- 未经用户明确授权，不得构建、打包、刷机或执行板端/Linux 命令。
- SDK 打包、镜像覆盖、刷机、板端验证可视风险分别设置授权门禁。
```

## 使用说明

复制上方 fenced text 块到新会话即可恢复完整状态。该文档记录 Route A 板端验证通过的事实、
当前主机产物证据、修正后的打包结论和下一阶段 Route B 目标。
不得声称整个 P2 或最终 RPMsg 数据面已完成。
