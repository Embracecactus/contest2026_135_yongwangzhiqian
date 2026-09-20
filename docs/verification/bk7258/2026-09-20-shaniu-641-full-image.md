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

## 边界与未闭合

- **待补**：安装后 `读取当前眼睛` 回读、以及**复位后再次读取确认保持**（这条用于
  验证第 2 项目录项持久化修复；此前正是在“装完即复位”后损坏）。
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
