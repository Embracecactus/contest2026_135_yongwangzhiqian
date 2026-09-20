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

同一设备随后安装第二个包 `shaniu-cyan-v3`（108,634 B）并再次复位，两段日志分别是：

```text
# 安装时
BKDISPLAY APP IMPORT transport=https result=0 bytes=108634
BKDISPLAY RENDER PASS … pack=shaniu-cyan-v3 revision=3 … fallback=0 sequence=2
# 复位后
BKDISPLAY RENDER PASS … pack=shaniu-cyan-v3 revision=3 … fallback=0 sequence=1
BKDISPLAY SERVICE READY dev=/dev/fb0,/dev/fb1 storage=/dev/mmcsd0 mount=short-lived
```

两次独立的“安装 → 复位 → 保持”都通过（`fallback=0` 表示标记与包都保留），
双屏在两种资源之间切换后均可跨复位保持。

**一次性观察（未再复现）**：在刚用 Windows 格式化过该卡之后，第一次
“安装 v1 → 直接复位”出现包与标记都不见了（解析器退回空卡等待，屏幕黑）；随后
的 v1 重装与 v3 安装都跨复位保持。差异是“格式化后的第一轮写入 + 立即复位”与
“后续轮次”。现有证据无法区分下列两种机制，因此本轮**没有**为它做投机式改动：

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
- 写入路径（`bkprov supply`）在实板上**未通过**：同一身份重放（CLI
  `voice pairing --direct-cloud --resume`，628 B BPI1，与设备内记录逐字节相同）
  完成 `BKPROV SUPPLY READY` 与全部 `NEXT offset` 后，在 commit 阶段被拒：

  ```text
  bk7258: error: identity supply failed: BKPROV_SUPPLY_ERROR stage=commit reason=target-rejected:-2002
  real  0m0.955s
  ```

  失败在 1 秒内返回（不是 90 秒 commit 等待窗口超时）且可重复；这一步是
  CP 控制台 → AP 本地 store，不经过网络，因此与 App 侧的认证状态无关。
  失败的是**未落盘的 pending 提交**，active 身份不受影响，`bkprov status` 仍为
  `identity=present bytes=628`。

### 同一身份重放的实板结果与取舍（641）

设计意图是幂等重放：store 对逐字节相同的记录返回 0、不重写，对不同记录返回
`-EEXIST`。实板观察到的第三种结果是 commit 阶段拒绝 `ret=-2002`。

- `-2002` **不是本仓库任何源码的返回值**（全仓 `grep -rIn -- "-2002"` 只命中
  无关外部库）；也不在主机侧 mbedTLS 3.4.0 复验的错误集合里——同一份 628 B
  记录在主机上 9 项全部通过（`ctr_drbg_seed`、`x509_crt_parse_der`、
  `pk_parse_key`、`pk_check_pair`、`serverAuth`/`clientAuth` EKU、
  `digitalSignature` KU）。因此该值只可能来自预编译厂商层（SDIO/RPMsg 或
  文件系统错误空间）或 AP store 对外部错误的原样透传。
- **本轮到此停止（未加 642 诊断）**：定位需要给 AP store 的
  `load/digest/unlink/open/transfer/fsync/close/rename/sync_directory` 各加一条
  `stage`+`ret`+`errno` 记录并重出整包固件（构建+全量烧录约 30–45 分钟），
  超出本次提交窗口；该路径只影响 CLI 重新供应身份，不影响已认领设备的
  产品链路。
- **对照证据（App 侧，用户执行）**：用户先复位设备，再在 App 内清除认证、
  重新认领，结果**成功**。串口同期可见 `AGENT service TLS verified=1 result=0`、
  `BKVOICE configuration ready=1 result=0 revision=1`、
  `BKVOICE official Trigger active label=nihao_openvela sha256=922eba91…`
  与 `BKVOICE wake model prepared=1 result=0`，说明设备内身份可用、唤醒模型
  重新装载（“认领成功”为用户报告，未落盘截图）。

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
- 未闭合：`bkprov supply` 的 commit 阶段在实板被拒 `ret=-2002`（见“同一身份
  重放的实板结果与取舍”）；定位需要 AP store 分阶段日志与一版诊断固件，
  本轮不做。已安装身份不受影响，App 侧清除认证 → 重新认领成功。
- App OTA 未在 641 重测（仍引用 `18.6.398+634`）。

## 证据位置

- 发布目录 `/tmp/bk7258-hil-20260920/aidk-release-full-641/`（`release.json`、
  `evidence/`、`flash/`、`package/`）。
- 构建日志 `/tmp/bk7258-build-641.log`；主机回归
  `tests/host/bk7258` 的 `run-display-pack`（编译 store）与 `run-display-rpc`。
- 串口：用户回贴文本，未落盘为原始文件。
