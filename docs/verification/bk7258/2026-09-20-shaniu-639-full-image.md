# 傻妞 AIDK 639：干净构建、实机启动与认领/交互验收

- 日期：2026-09-20（Asia/Shanghai）。
- 构建、签名、发布与本记录：Codex；烧录、App 认领与语音/拍照操作：用户（owner）。
- 固件身份：`18.6.401+639`，board `aidk_ai_toy`，profile `app__openvela_ap`，
  artifact id `shaniu_aidk_v18_6_401_639_full`，security counter 与 rollback floor 639。
- 构建输入：`--clean` 全量重建，源码 `df87a94b`（`dirty=false`，inputs 565）；
  之后只有文档提交（`2905ad84` 及本文件），不改变镜像内容。

## 包身份

| 项 | 值 |
| --- | --- |
| operator 镜像名 | `shaniu-bk7258-aidk_ai_toy-app__openvela_ap-v18.6.401+639-bshaniu_aidk_v18_6_401_639_full-full.bin` |
| operator 大小 / SHA256 | 8,388,608 B / `40315149719b8fff0f6335ec18c9a1ef870b350f05b77bc59611700854c81dcb` |
| `.bkpack` 大小 / SHA256 | 7,980,186 B / `ce87548ba9e50862f82d43bff30ce46397144869f3e5f39b8144c2831e8b50a5` |
| `release.json` / build manifest SHA256 | `aa3e55eb55d6a72843689324da2859d0de89c8c5f0fe309f8b9c768b0b79ea8d` / `cc688461dbd28d4bb4a1fab9338f6ea5f11e6fdc7a42dd6812f48052116e4ac7` |
| accepted base（同板） | `b713989ed04c664f486e5f88e00b699d179c42f3b9607718bf38ee47499da91f`（与 637/638 同一块基线） |
| raw 镜像 | CP 1,112,068 B；AP 1,615,448 B（相对 638 增加 `bkprov`/`bkdata` 命令与 `bkprov-v1` 服务） |
| 签名身份 | BL1 `58e384ae…`、MCUboot `4979ece7…`（未轮换） |

## 新增能力（本镜像）

- CP 命令 `bkprov supply|status`：写入/查询本板 BPI1 身份（经 `bkprov-v1` 转发给
  AP 侧 store，复用既有校验与“不覆盖不同身份”语义）。
- CP 命令 `bkdata status|init --confirm erase-non-littlefs`：仅在 `persistent_data`
  不是有效 LittleFS 时格式化，不自动执行、不动 SD NAND/校准尾区。

## 实机验收（用户回贴串口 + 口头确认）

启动（用户回贴）：

```text
B1PRIMARY / B2INIT / B2GOOK / B2APOK
BK7258 SYSINIT PASS
BK7258 FINALINIT PASS
BK7258 RCS PASS
BKPROV SERVICE READY endpoint=bkprov-v1
AIDK DEFERRED DONE failures=0 elapsed=3440 ms
BKDISPLAY RENDER PASS expression=neutral pack=shaniu-cyan-v2 revision=2 screens=2
BKVOICE official Trigger active label=nihao_openvela sha256=922eba91… bytes=23640
```

- **启动**：SYSYINIT/FINALINIT/RCS 全 PASS，`AIDK DEFERRED DONE failures=0`；默认眼睛包
  （`shaniu-cyan-v2` revision 2）与默认唤醒模型（`nihao_openvela`，23,640 B）随镜像
  就绪，首装后无需再更新资源即可交互。
- **认领**：第一次尝试失败，设备返回 `-100`；日志显示 `ASSOCIATED → 4WAY_HANDSHAKE`
  反复中断并以 `BKVOICE PROVISION stage=wifi_result ret=-110` 结束——`-100` 即
  `-ENETDOWN`，来自认领事务里的 Wi-Fi 试验（`bk7258_provision_network.c` 的
  `poll_trial`），不是身份/持有秘密失败（认证/校验失败会是 -9/-12）。
  用户修正 Wi-Fi 密码后**认领与配网成功**。
- **交互**：用户确认**语音唤醒成功、“我在”应答与拍照成功**。
- **眼睛资源导入/切换**（两次 App 导入均有串口证据）：
  - 装 `shaniu-default-v1` 成功并切换生效：
    `BKDISPLAY APP IMPORT transport=https result=0 bytes=10494` →
    `BKDISPLAY RENDER PASS … pack=shaniu-default-v1 revision=1 screens=2`；
  - 再装已存在的 `shaniu-cyan-v2` 返回 `BKDISPLAY APP IMPORT … result=-17`
    （设备端 `-EEXIST`，`bkdisplay_store_install()` 对已存在的 `<pack_id>.bkep`
    拒绝重复安装），属预期行为；
  - 当前产品行为 = “装新包即新增并激活”，**没有切换已装包/删除包按钮**；要在两套
    外观之间来回切，需每次使用新的 `pack_id`。为演示切回青色，已生成
    `shaniu-cyan-v3`（108,634 B，SHA256
    `cf9dff38d34ff220503021fdea68dcf4b5942a1467f8fbe9c395f0d97653ee56`，
    同素材、新包标识），并通过 ADB 放到手机 `/sdcard/Download/`。
- **观察到的告警**：两次导入期间的串口出现成片的
  `[media][media_recorder_queue_push:290] data queue is more than max count(12)`
  （WARN，队列满丢帧提示；同一窗口内 KWS 仍持续输出 `windows/scores`），
  本记录不声称其根因或影响，仅如实记录。

## 验收中发现的缺陷（已在 640 修复）

- **现象**：装入新包 `shaniu-cyan-v3` 成功、随后复位，启动时
  `BKDISPLAY STORE stage=pack-validate path=…/packs/shaniu-cyan-v3.bkep fallback=0 ret=-5`
  → `BKDISPLAY RENDER FAIL stage=store-resolve ret=-5` →
  `BKDISPLAY START FAIL stage=neutral-render ret=-5`，双屏保持黑屏。
  `-5` 是 `-EIO`（`bkdisplay_pack_open()` 的读失败，不是 CRC/格式错误）。
- **直接原因**：`bkdisplay_store_resolve_layout()` 只在 active 标记**缺失**时回退到
  默认包；标记存在但包读不了时直接返回错误，服务因此启动失败，没有回退到板上
  其它已安装且有效的包。
- **诱因（未完全归因）**：安装/切换只对**文件**做了 fsync，`rename` 后的目录项没有
  同步；在装上大包后立刻复位，目录项/簇链可能未落盘，下一次启动读取失败。
- **修复（640）**：① active 包校验失败时只读回退——先试默认包
  `shaniu-default-v1.bkep`，再扫描 `packs/` 取第一个有效包，标记文件不改写；
  ② 安装与切换在 `rename` 后同步 `packs/`、`staging/` 与 display 目录项。
  主机回归 `make run-display-pack`（编译该 store 文件，`-Werror`）通过。
- **639 上的即时恢复**（无需重刷）：经 native USB MSC 导出 SD NAND，删除
  `shaniu/display/active.json`（或同时删除损坏的 `packs/shaniu-cyan-v3.bkep`），
  安全退出后重启——现有的“标记缺失→默认包”回退会渲染已安装且有效的
  `shaniu-default-v1`。
- **第二类失败（同一台设备后续复位）**：`BKDISPLAY RENDER FAIL stage=volume-open
  ret=-22`（`-EINVAL`，`mount(…, "vfat", …)` 被 FAT 卷拒绝）→
  `BKDISPLAY START FAIL stage=neutral-render ret=-22`。此时连卷都挂不上，
  说明 SD 的 FAT 已不可挂载，需要 PC 侧修复（见下）。同时暴露一个固件缺陷：
  `bkdisplay_service_retryable()` 不含 `-EINVAL`/`-EIO`，显示 worker 遇到这两类
  存储错误会**直接退出**，修复卡后也必须重启才能恢复。
- **641 加固**：`bkdisplay_volume_open()` 挂载失败时记录原始 errno 并以可重试的
  `-EAGAIN` 返回；`-EIO` 纳入可重试集合——服务改为 `WAITING_ASSET` 周期重试
  （500 ms），卡修好或重新插好后**无需重启**即可自动恢复渲染。

## 边界与未闭合

- App OTA 未在 639 重测（仍引用 `18.6.398+634`）。
- 唤醒/拍照为操作者口头确认；本记录不含该轮完整串口摘录，未做真人泛化/功耗/长稳结论。
- 眼睛“换包”演示需使用不同 `pack_id` 的包（见
  [首次部署输入清单](../../platforms/bk7258/first-deployment-inputs.md) 第 3 节）；当前无删除/回退按钮。
- 639 的 `bkdata init` 未在新板（非 LittleFS 的 `persistent_data`）上实测。

## 证据位置

- 发布目录 `/tmp/bk7258-hil-20260920/aidk-release-full-639/`（`release.json`、
  `evidence/`、`flash/`、`package/`）。
- 构建日志 `/tmp/bk7258-build-639.log`（`--clean` 两个角色，层门禁 PASS 506 源）。
- 串口：用户回贴文本，未落盘为原始文件。
