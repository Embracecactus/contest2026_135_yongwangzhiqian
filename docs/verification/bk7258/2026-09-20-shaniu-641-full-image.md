# 傻妞 AIDK 641：显示资源存储加固与实机恢复验证

- 日期：2026-09-20（Asia/Shanghai）。
- 构建、修复与本记录：Codex；烧录、格式化 SD、App 安装与实机操作：用户（owner）。
- 固件身份：`18.6.401+641`，board `aidk_ai_toy`，profile `app__openvela_ap`，
  artifact id `shaniu_aidk_v18_6_401_641_full`，security counter / rollback floor 641。
- 源码 `3d68b447`（`dirty=false`），operator 8,388,608 B
  SHA256 `5a70b0769e80ba2c5c7697e25a06a0dbebacf4365bfd8d4380202a8b96f1e830`；
  `.bkpack` `5a37fe5b70fd627a458f3e341081b97e3d122a2dcc8aead5ef8544fc67523d06`。

## 本版本修复的三类显示存储缺陷

1. **active 包读失败无回退**（640，`bc925abe`）：active 标记指向的包校验/读取失败时，
   只读回退到默认包，再扫描 `packs/` 取第一个有效包，标记文件不改写。
2. **安装/切换的目录项不持久**（640）：`rename` 之后同步 `packs/`、`staging/` 与
   display 目录项（文件数据此前已 fsync）。
3. **存储错误不可重试**（641，`59609db8`）：`mount()` 失败与包读 `-EIO` 原先不在
   `bkdisplay_service_retryable()` 中，显示 worker 直接退出；现在挂载失败记录原始
   errno 并返回可重试的 `-EAGAIN`，`-EIO` 纳入可重试集合，服务保持
   `WAITING_ASSET` 每 500 ms 重试。

## 实机恢复与安装（用户回贴串口）

修复前的失败序列（639，同一台设备）：`pack-validate … ret=-5`（单包读 EIO）→
随后 `stage=volume-open ret=-22`（FAT 卷被 `mount()` 拒绝）。用户的操作与结果：

```text
# 641 上：挂载失败进入可重试等待（约每 500 ms，不再 START FAIL 退出）
BKDISPLAY VOLUME stage=mount ret=-22
BKDISPLAY VOLUME stage=mount ret=-22
...
# 导出 SD 到 PC 修复/格式化
nsh> usbmode msc
usbmode: exporting /dev/mmcsd0; keep it unmounted on AP while MSC is active
BK7258 USBMSC: ready blockdev=/dev/mmcsd0 sectors=247808 size=512
BK7258 USBMODE: cdc -> msc
BKDISPLAY RENDER WAIT stage=volume-open ret=-16      ← MSC 持有卡，-EBUSY 属预期
...
# 格式化完成、安全弹出后切回 CDC；空卡进入“等待资源”
BK7258 USBMODE: msc -> cdc
mode=cdc
BKDISPLAY STORE stage=active-read path=/mnt/sdnand/SHANIU/DISPLAY/active.json ret=-20
BKDISPLAY RENDER WAIT stage=store-resolve ret=-20    ← -ENOTDIR：空卡无包，可重试等待
...
# App 安装 shaniu-default-v1（10,494 B）
BKDISPLAY RENDER PASS expression=neutral pack=shaniu-default-v1 revision=1 screens=2 mapping=unverified fallback=0 sequence=1
BKDISPLAY APP IMPORT transport=https result=0 bytes=10494
```

结论：**格式化后的卷可挂载**；服务在“无包”状态保持重试而不是退出；App 安装
`shaniu-default-v1` 返回 `result=0` 并立即渲染双屏。

## 复位持久性（已实测，含一次未闭合的观察）

安装 `shaniu-default-v1` 后经
`usbmode msc` → PC 侧确认 `X:\shaniu\display\packs\shaniu-default-v1.bkep` 存在 →
安全弹出 → `usbmode cdc` → **复位**，启动日志：

```text
BKDISPLAY RENDER PASS expression=neutral pack=shaniu-default-v1 revision=1 screens=2 mapping=unverified fallback=0 sequence=1
BKDISPLAY SERVICE READY dev=/dev/fb0,/dev/fb1 storage=/dev/mmcsd0 mount=short-lived
```

`fallback=0` 说明 **active 标记与包文件都跨复位保留**，双屏正常点亮。

**未闭合观察（同一台设备、更早一次）**：在刚用 Windows 格式化过该卡之后，第一次
“安装 v1 → 直接复位”出现包与标记都不见了（解析器退回空卡等待，屏幕黑）；随后
重新安装并做了一轮 MSC 导出/安全弹出再复位，就稳定保留。两者差异是
“格式化后的第一轮写入 + 立即复位”与“再次写入 + 额外一次卷释放/时间间隔”。
现有证据无法区分下列两种机制，因此本轮**没有**为它做投机式改动：

1. NuttX FAT 的 `fat_unbind()`（umount）不刷文件系统缓冲（源码注释承认会丢数据），
   与卡内写缓存的组合可能在“写后立即断电”时丢失最近的目录/FAT 扇区；
2. 刚被 PC 格式化过的卷在 NuttX 侧的第一轮写入可能只落到单份 FAT 副本或与
   FSINFO/备份引导扇区不一致，第二次挂载才收敛。

**运维规则（已写入首次部署输入清单）**：格式化 SD 之后，先执行一次
“安装包 → 复位 → `读取当前眼睛`”的验证循环再信任该卡；若复位后包消失，
重新安装一次并记录日志，不要用再次格式化掩盖。

## 身份通道与内部存储（641 实板）

用户在同一台设备的 NSH 上执行新命令：

```text
nsh> bkprov status
BKPROV STATUS identity=present bytes=628
```

这一条同时证明：

- **CP 内部 LittleFS 跨复位保留**：身份记录（628 B BPI1，与 2026-09-13 经有线通道
  供应的记录大小一致）在多次复位后仍存在，说明内部 /data 的写入是持久的——
  之前启动日志里的 `persona … source=default` 只是 App 未重发配置，不是存储丢失；
- **新 `bkprov` 通道在实板上打通**：CP 命令 → `bkprov-v1` RPC → AP 侧 store →
  回读状态，端到端工作（本轮验证的是只读 STATUS 路径）；
- 写入路径（`bkprov supply`）**尚未实板验证**；可用“同一身份重放”做幂等验证：
  `voice pairing --direct-cloud --resume` 会发送与设备内完全相同的 BPI1 记录，
  store 对逐字节相同的记录返回 0、不重写（不同记录返回 `-EEXIST`），因此不会
  改动设备身份。

## 边界与未闭合

- **待补**：安装后 `读取当前眼睛` 的 App 回读（串口侧已确认 `fallback=0` 与
  `RENDER PASS`）。
- 观察到的告警：导入窗口仍有成片的
  `[media][media_recorder_queue_push:290] data queue is more than max count(12)`
  （WARN），本记录不声称根因或影响。
- 观察到的噪声：空卡等待期间每次重试都会打印一行历史树探测
  `stage=active-read path=…/SHANIU/DISPLAY/active.json`（INFO）；仅日志噪声，
  不在本版本处理。
- FAT 被写坏的确切机制未完全归因（最可能是“装完大包立即复位”打断目录/簇链落盘，
  随后逐次恶化）；本版本的防线是回退 + 目录同步 + 可重试。
- App OTA 未在 641 重测（仍引用 `18.6.398+634`）。

## 证据位置

- 发布目录 `/tmp/bk7258-hil-20260920/aidk-release-full-641/`（`release.json`、
  `evidence/`、`flash/`、`package/`）。
- 构建日志 `/tmp/bk7258-build-641.log`；主机回归
  `tests/host/bk7258` 的 `run-display-pack`（编译 store）与 `run-display-rpc`。
- 串口：用户回贴文本，未落盘为原始文件。
