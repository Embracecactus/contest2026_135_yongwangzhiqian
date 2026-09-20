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
- **眼睛资源导入**：再装同一个 `shaniu-cyan-v2` 返回 `-17`（设备端 `-EEXIST`，
  `bkdisplay_store_install()` 对已存在的 `<pack_id>.bkep` 拒绝重复安装），属预期；
  当前设备已激活该包，不需要重装。

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
