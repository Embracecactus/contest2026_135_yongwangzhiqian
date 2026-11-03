<!-- SPDX-License-Identifier: Apache-2.0 -->
# 傻妞功能与证据清单

本清单把每项产品能力连到它的使用方式、运行位置、实现位置和最高证据。
板级当前验收结论只在 [CONFIGS.md](../../../boards/bk7258/CONFIGS.md) 维护；
行为合同见[桌面伙伴合同](shaniu-desktop-companion.md)，具体数据以日期化
[verification 记录](../../verification/bk7258/)为准。本文于 2026-11-04 核对：
官方分支头为 `906a38d3`（[官方 build/verify-delivery push 运行成功](https://github.com/open-vela/contest2026_135_yongwangzhiqian/actions/runs/41676359713)），PR #140 已合入，合并后 `b21fa804`
的 build 与 verify-delivery 均成功；所选提交的 Actions 结果仍须在下载或演示前复核。

**证据层级**：实现（代码存在）→ 构建（固件/APK 可编译链接）→ 主机测试 →
模拟器 → 实板观察（串口/回执）→ 真机手机 → 人工确认（看到、听到、摸到）。
历史版本的实板结论只对该版本有效，不能推出当前源码已通过。

缩写：`app/` 指 `app/bk7258/`；`tools/` 指 `tools/bk7258/`；`apk/` 指
`android/shaniu-companion/app/src/main/java/com/shaniu/companion/`；Agent 指
manifest 固定的 `packages/ai_agent`（`76c65bc6`）。

## 语音与智能

| 能力 | 使用与反馈 | 运行位置与依赖 | 实现 | 最高证据 | 剩余边界 |
| --- | --- | --- | --- | --- | --- |
| 本地唤醒 | 说“你好，openvela”；眼睛转为聆听，可播放预置“我在” | AP 上的 TFLM，32 通道 INT8 DS-CNN 23,640 B；不需网络 | `app/bk7258_voice_kws*.c`、`bk7258_agent_trigger.c`、`app/models/` | 实板：638 完整链路；11-03 真人唤醒两次本地命中（976/1000、859/1000） | 无真人多人误/漏唤醒统计；手机外放两次未触发（11-01） |
| 连续问答与追问 | 唤醒后提问；播放排空后免唤醒追问；静默超时回待机 | 云端 ASR → Agent/LLM → 流式 TTS；需 Wi-Fi 与云配置 | Agent Session/Voice；`app/bk7258_agent_product.c`、`bk7258_cloud_*.c`、`bk7258_agent_media_*.c` | 实板：638（09-20）。11-03 缺网络与云配置时按设计不启动（`ENETDOWN`），非模型故障 | 当前版本完整语音未实板验收；真实云时延与首声未测 |
| 流式首句与取消 | 首句可先播；内部取消路径可停止播放器，但播放期间不接收 MIC，不能宣称语音打断 | 同上 | Agent 流式路由、`bk7258_agent_final_stream`、`bk7258_agent_trigger_reply_cancel` | 工程 HIL（701，固定响应）正常/取消/下一轮；主机测试 | 真实云性能、取消总耗时上界、可面向用户操作的播放中取消尚未证明 |
| 本地固定指令 | 音量、表情、停止、专注、启动小功能；命中后不请求 LLM/TTS | 仍需云端 ASR；执行在设备上；播报排空后才会恢复采集 | `app/bk7258_local_intent.c`、`bk7258_agent_local_intent.inc` | 主机测试（`run-agent-local-intent`）+ AIDK 构建 | 新指令未实板验收；不是离线语音或播放中语音打断 |
| 拍照识物 | “看看这个”；LLM 选择 `camera_capture`，640×480 JPEG 进入同一会话 | AP 相机单所有者；云端多模态 | `app/bk7258_agent_vision.c`、`bk7258_vision_*.c` | 实板：639 用户确认；历史 20,251 B 帧 | 有颜色识别错例；无准确率统计 |
| 回答表情 | LLM 调 `reply_expression` 设置本轮表情，播报时显示 | 设备显示服务 | `app/bk7258_reply_expression.c` | 主机测试 + 构建（11-01） | 表情与声音的物理同步误差未测 |
| 回答长度/思考/模型/音量/语速 | App 设置，重启保留；本轮明确要求优先 | 设备偏好存储；云模型遵循程度取决于服务 | `app/bk7258_agent_profile.inc`、`bk7258_preferences*.c`；`apk/provision/DeviceSettingsEditor.kt` | 主机测试；11-01 真机读写与重连 | 模型实际遵循长度未在真实服务上统计 |
| 事实记忆 | 开记忆后“记住我叫…”“我喜欢…”；App 查看、纠正、删除 | 设备 AES-GCM 加密快照，投影到官方 Session | `app/bk7258_agent_memory*.c`、`bk7258_memory_facts.c` | 真机 Mi 10 + 实板：11-01 新增/纠正/删除/重启回读（虚构值） | 真实跨天记忆未统计 |
| Runtime Skill | “专注助手/设备体检/识物解说：…”；App 启停与导入签名包 | 设备本地；外部包 P-256 签名，默认停用 | `app/bk7258_runtime_skills*.c`；`tools/` `skill pack` | 真机 + 实板：11-01 启停重启保持；635 安装 | 三场景实机完成率未统计 |

## 显示、动作与实体交互

| 能力 | 使用与反馈 | 运行位置与依赖 | 实现 | 最高证据 | 剩余边界 |
| --- | --- | --- | --- | --- | --- |
| 双眼表情与状态 | 系统状态优先于情绪；聆听/思考/说话/离线；眨眼与左右看 | 双 QSPI 160×160 圆屏，SD NAND 资源 | `app/bk7258_display_*.c`、`app/assets/display/` | 实板：641 两套资源跨复位保持；11-03 happy→neutral 渲染回读 | 当前双眼外观与左右映射待人工确认 |
| 动作联动表情 | 拿起、放稳、倾斜时短暂表情（≤800 ms 意图） | SC7A20H，≥100 ms 采样；语音、专注等优先 | `app/bk7258_motion_*.c` | 实板：11-03 加速度样本持续更新 | 拿起/放下与表情的关联未人工确认；阈值未按真人手势调参 |
| 完成触觉 | 专注/电脑任务/本地内容完成时 40 ms 脉冲，2 s 冷却；说话/收音时抑制 | 振动马达，共享供电投票，≤3400 mV 抑制 | `app/bk7258_haptic_*.c`、`bk7258_agent_companion.inc` | 实板：11-03 任务反馈两次驱动返回 0，采集恢复 | 实际振感未确认；串口直接脉冲 `-16` 为麦克风互锁预期拒绝 |
| 专注计时 | 语音/App/NFC/电脑开始、暂停、继续、取消；进度环与剩余时间 | 设备单一计时器；重启不恢复运行中计时 | `app/bk7258_focus*.c`、`bk7258_display_focus.inc` | 模拟器（App 草稿、取消、恢复）+ 主机测试 | 完整实板计时与反馈回读未做 |
| NFC 场景卡 | App 把卡登记到 8 个槽：专注 1–4、本地内容 5–6、启动小功能 7；碰卡执行 | MFRC522，ISO14443A UID；一次一张卡，移开再碰 | `app/bk7258_nfc_*.c`；`apk/` 场景卡片 | 模拟器登记草稿；实板无卡扫描 `-110` | 实卡登记、触发、异常路径均未实测；无卡超时不代表模块损坏 |
| 本地内容 | 播放原创 4 秒“四拍节律”（内容 ID 1） | ROMFS 只读 PCM，SHA256 校验后由 Media 播放 | `app/bk7258_local_content.c`、`app/assets/local_rhythm.pcm` | 主机测试 8 个场景 | 实板播放与停止未实测 |
| 签名原生小功能 | “帮我做个决定”；App 安装第一方签名包；定时、存档、结果标签（ABI 3） | AP，CPU0 执行加载生命周期；≤16 KiB ELF，每步 20 ms 预算 | `app/bk7258_native_*.c`、`app/native-apps/`；`tools/` `native-app` | 主机测试；诊断配置下测试签名的实板安装/启动、越权拒绝 | 产品 `CONFIG_BK7258_NATIVE_PUBLISHER_SHA256` 为空，正式固件拒绝安装；不是安全沙箱 |
| 按键 | K1/K3 音量；K2 单独按住 ≥3 s 松手请求软关机 | 本地 GPIO | `app/bk7258_product_keys.h`、`bk7258_agent_keys.c` | 实板：638 音量键 | K2 关机/开机未单独验收；恢复出厂只走 App |

## 手机 App

| 能力 | 使用与反馈 | 运行位置与依赖 | 实现 | 最高证据 | 剩余边界 |
| --- | --- | --- | --- | --- | --- |
| 发现与认领 | 未认领设备自动开放首认领窗口后扫屏幕认领码；广播候选不等于已认证；结果不确定时“核对认领结果” | BLE GATT 上 TLS 1.2（证书 pin），无需互联网；普通按键是 PTT，旧 8 秒按住/松开仅回执恢复 | `app/bk7258_provision_*.c`；`apk/provision/` | 真机：637 认领、641 清除后重新认领；10-18、11-01 Mi 10 | 新板工厂首启 + 新版 App 首认领完整路径未验收 |
| 配网与云服务 | 板子扫描 Wi-Fi，填写密码与云服务；保存后回读；当前默认聊天模型 `mimo-v2.6-flash` | 认证 BLE；密码与 Key 不回读 | `apk/provision/DeviceSettingsEditor.kt` | 真机历史记录；模拟器 11 个配网异常场景；11-04 Mi 10 上 `0.7.28-shaniu-mimo-chat` / code 58 云配置保存并回读、设备报告 Wi-Fi 就绪 | 11-04 只是设置状态；不证明当前语音、真实 BLE 生命周期或 OTA |
| 定制页 | 眼睛资源、默认表情、限时试用、唤醒模型、回答与唤醒 | 资源经局域网 HTTPS；模型经认证 BLE | `apk/`；`app/bk7258_display_*`、`bk7258_voice_wake_package.c` | 实板：641 资源安装；模拟器草稿/取消/默认选择 | 新版本实机安装与回读未做 |
| 固件更新 | 选择 `.bkpack`，保持前台；重连核对版本/计数才显示完成；中断后先查询结果 | 局域网 HTTPS + 签名、防回滚 | `apk/ota/OtaPackageServer.kt`；`app/bk7258_agent_ota.c`、`bk7258_voice_ota_*.c` | 实板：634 首次 OTA；767 升级与自动重连通过；770 升级成功但自动重连失败 | 来源上传期取消未实测；OTA 后重连仍为专项 |
| 电脑授权 | 导入电脑配对请求，核对摘要，授予权限，导出加密响应 | 认证 BLE；电脑端 DPAPI 保存 | `apk/`；`app/bk7258_pc_authorization*.c` | 主机测试 + 模拟器文件确认/取消 | 手机→电脑完整导出导入的实机纵向验收未做 |
| 电脑审批 | 等待 owner 决定时傻妞亮“!”，逐字核对，批准或拒绝；批准后清除提示 | 只有已认证 owner；超时/改动按拒绝 | `apk/provision/PcApprovalController.kt`；`app/bk7258_pc_approval.c` | 主机 C/Python/Android；11-04 `0.7.129+775` 实板：等待批准时双眼感叹号、批准后清除，回执 approved、authenticated=true、返回码 0 | 拒绝、超时和多轮稳定性未在 775 复测 |
| 恢复出厂 | 已认证 App 提交持久撤销；结果不明时只查询回执 | 设备 + 手机 | `apk/`；`app/bk7258_reset_main.c` 等 | 主机/模拟器 | 实际多介质清理与重新扫码未验收 |
| 界面 | 设备/定制/更新/设置；深浅色、200% 字体、横屏、减少动画 | Material 3 View | `apk/`、`android/shaniu-companion/DESIGN.md` | 模拟器：两主题各 30 场景 + 20 轮导航 | 真机无障碍未单独评估 |

## 电脑工作台

| 能力 | 使用与反馈 | 运行位置与依赖 | 实现 | 最高证据 | 剩余边界 |
| --- | --- | --- | --- | --- | --- |
| 真实任务 | `workbench task-run --exec ...`；进程退出码决定成功；Ctrl+C/超时取消 | 电脑执行；原生 USB CDC + TLS；`tasks` 权限 | `tools/_lib/workbench_task_runner.py`；`app/bk7258_pc_tasks.c` | 实板：11-03 正常 0、超时 `reason=timeout` CLI 130，均 `device_result=confirmed` | 屏幕与振感反馈未人工确认（`feedback_verified=false`） |
| 按需取景 | 浏览器“采一帧”、保存、取消、重拍、释放缓存 | 单帧 640×480 JPEG，512 B 分块；`camera` 权限 | `tools/_lib/workbench_camera.py`；`app/bk7258_pc_camera.c` | 实板：11-03 两帧 8,583/11,767 B，帧传输 1.413/2.020 s；保存哈希一致；55 秒录屏 | 画面偏暗；并发与长稳未测 |
| 资源作业 | 导入眼睛包、试用、默认选择、目录分页 | `resources`/`scenes` 权限 | `tools/_lib/workbench_resources.py` 等 | 主机：HTTP → 真实客户端 TLS → 原生安装作业 | 11-03 当前 profile 被拒 `-13`（未授权），正向实板未做 |
| 审批请求 | `approval-request`/`approval-run`/`approval-hook`（Claude Code `PreToolUse`） | 电脑只能请求；回执以电脑授权密钥 HMAC | `tools/_lib/workbench_approval.py` | 同“电脑审批” | 同上；语音不能发起电脑操作 |

## 平台、交付与研究

| 能力 | 说明 | 实现 | 最高证据 | 剩余边界 |
| --- | --- | --- | --- | --- |
| CP/AP 双镜像与 SMP | CPU0 运行 CP，CPU1/CPU2 运行 AP SMP；RPTUN/RPMsg 跨核服务 | `chips/bk7258/` | 多代实板启动；RPMsg 80 轮压力；原生加载 CPU0 门禁两条路径各 100 轮 | 755/756/770 CPU1 心跳停止无共同根因；底层跨核锁问题间歇存在 |
| 启动链与签名升级 | 自研 BL1 → MCUboot BL2 → 同槽签名 CP/AP，A/B，防回滚 | `boards/bk7258/`、`tools/_lib/image.py`、`package.py`、`trust.py` | 多次实板 OTA 与整机烧录；CI 冷构建验签 | OTP/eFuse 与新板首装未完整验收 |
| 三板复用 | AIDK 傻妞、T5-Board Dolphin、T5AI-Core 基线 | `boards/bk7258/<board>/` | 三板 direct 构建；Dolphin `0.1.0+13` 录音落卡由用户确认 | 后续 Dolphin 新包触摸与录音未验证 |
| 性能基线 | T5-Board 性能配置 CPU0 240 MHz CoreMark 561.58（10 次） | `boards/bk7258/t5_board/configs/perf` | [实板记录](../../verification/bk7258/2026-08-27-bk7258-sdk-clock-240m-validation.md) | 非 AIDK 产品负载 |
| 唤醒模型训练 | 审计、训练、量化、TFLM 对齐、来源隔离评测 | `tools/_lib/voice_kws*.py` | 本地研究：多轮候选，合成留出测试有提升也有退步 | 无候选进入产品；真人与板端验收缺失；研究记录未公开 |
| TTS 供应商对比 | MiMo SSE 对比腾讯云 WebSocket，同板同输入 | `app/bk7258_tencent_tts.c`（仅测试配置） | 本地研究 R7：5 次小样本，保留 MiMo | 不是普遍排名 |
| Wi-Fi CSI | 独立诊断配置采集原始 CSI 并导出到电脑 | `csi_cp`/`csi_ap`；`tools/_lib/csi.py` | [实板 60 s 1,379 帧无缺帧](../../verification/bk7258/2026-10-31-wifi-csi.md) | 只有采集，没有存在/动作识别结论 |
| 云端冷构建 | 干净 manifest 同步、开发身份签名构建、App、回归、独立复核 | `.github/workflows/shaniu-source-checks.yml` | PR #140 合并后 `b21fa804`：build 与 verify-delivery 成功 | artifact 只保留 7 天；开发身份包不等于作者签名发布，也不证明实板 |

## 失败与修复

保留下来的失败及其处理，按日期：

| 日期 · 版本 | 现象 | 处理与当前状态 |
| --- | --- | --- |
| 08 月 · 早期 bring-up | UART 能出不能进；SDK “OPP 320M” 实为 CPU0 160 MHz；RPMsg 第 53 轮 `ENOMEM` | 逐层修复 RX；按实测频率；改常驻 worker，80 轮无失败（[技术报告 3.4.5](../../contest/技术报告-BK7258三核适配与傻妞AI伴侣.md)） |
| 09-20 · 639 | 干净构建暴露显示卷挂载/读包 `EIO` | 641 修复，两次安装复位保持；身份写入 `-2002` 仍未通过 |
| 09-23 · 649 | 启动 OOM | [启动修复候选](../../verification/bk7258/2026-09-23-cp-startup-repair.md) |
| 10-18 · 755/756 | CPU1 心跳停止（`SECONDARY_TIMEOUT`） | 无共同根因，保留为未闭合 |
| 10-18 · 766/770 | 升级后蓝牙重连/扫描失败 | 767 通过；770 广播停止超时导致自动重连失败，未闭合 |
| 10-19 | MTU 握手超时、`PRIMARY_TIMEOUT` | [修复记录](../../verification/bk7258/2026-10-19-mtu-primary-timeout-repair.md) |
| 11-02 | CPU1 文件 I/O / RPMsg 锁丢失与计数下溢 | 限定 CPU0 执行加载生命周期；不宣称底层修复 |
| 11-03 | 真人唤醒命中但对话 `ENETDOWN` | 设备只保存了 owner，缺网络与云配置；需手机补齐后再验 |
| 11-03 | 录音正常结束误报错误 | 媒体依赖 `eee16420` 修复并固定到 manifest；906 官方冷构建通过，当前整机语音回归仍待完成 |
| 11-03 | 模拟器配网异常组合超时 | 定位到模拟器图形层，软件渲染重启后完整通过；失败录像保留 |
| 11-03 | CI 合同步骤 `SETUP_ERROR` | GCC 13 `-Waddress` 与测试 `-Werror` 组合；测试编译选项已修复，`b21fa804` 及 `906a38d3` 官方 CI 均通过 |

## 需要人工配合的验收

按优先级：已绑定手机补齐网络与云服务配置后，验证唤醒、追问、停止和识物；
手机拒绝一次；兼容 NFC 卡登记与触发；拿起/倾斜与完成振感；资源授权后
装包与回读；最后单独安排 OTA 与升级后重连。逐条步骤见本地答辩材料中的拍摄与
验证清单。
