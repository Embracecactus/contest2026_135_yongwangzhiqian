# 功能与镜头对照（FEATURE_MAP）

> 由 `tools/build_feature_map.py` 从 `script/features.json`（功能清单与证据）和 `src/edit.js`（片中位置）生成；时间为成片时间（已计入剪辑的放慢段）。
> **三层分开看：** “片中如何出现”是动画演绎；“实现”列出源码位置；“证据”只转述仓库里已有的验证记录，片中画面本身不是证据。片中等待经过剪辑，不代表实测时延。

| ID | 功能 | 可用性 | 片中位置（场景 · 成片时间） | 片中如何出现 | 实现 | 证据（来自仓库记录） |
| --- | --- | --- | --- | --- | --- | --- |
| F01 | 首启认领码与扫码认领 | 已实现 | s03 做她 · 0:49.8–1:01.6 | 开发板刷入后首启：双眼显示认领码；App 扫码认领 | app/bk7258_provision_claim.c<br>app/bk7258_display_snapshot.inc（claim_qr）<br>apk/provision/ProvisionActivity.kt<br>apk/DevicePage.kt | 真机：637 认领、641 清除后重新认领、10-18/11-01 Mi 10；新板工厂首启 + 新版 App 首认领完整路径未验收 |
| F02 | 核对认领结果 | 已实现 | s04 认领与配网 · 0:57.0–1:01.6 | 正在连接并验证设备 → 已验证连接傻妞 | apk/DevicePage.kt（核对认领结果）<br>guide §A | 实现 + 真机历史流程；本片不单独演示失败分支 |
| F03 | Wi-Fi 扫描与配网 | 已实现 | s04 认领与配网 · 1:01.6–1:07.0 | 设备扫到的 Wi-Fi 列表，输入密码（“屿和晴的家”） | apk/provision/DeviceSettingsEditor.kt<br>app/bk7258_provision_scan.c<br>app/bk7258_provision_network.c | 真机历史记录；模拟器 11 个配网异常场景通过；11-04 Mi 10 设置保存并回读 |
| F04 | 云服务与模型（听 / 想 / 说） | 已实现 | s04 认领与配网 · 1:07.0–1:11.0 | 听 / 想 / 说三项云服务，保存后等待设备回读 | apk/SettingsPage.kt（云服务与模型）<br>apk/provision/CloudSettings.kt<br>README.md §第一次使用 | 11-04 App 0.7.28 / code 58 云配置保存并回读；设备报告就绪 |
| F05 | 连接链与帮助诊断 | 已实现 | s04 认领与配网 · 1:11.0–1:12.2<br>s11 天亮 · 7:09.8–7:12.0 | 连接链：手机 ↔ 傻妞 ↔ 语音服务<br>改名后连接链断开一段：设备未联网 | apk/DevicePage.kt<br>apk/SettingsPage.kt（帮助与诊断）<br>android/shaniu-companion/DESIGN.md（connectionChain） | 模拟器两主题各 30 场景；真机历史 |
| F06 | 更换 Wi-Fi（不必重新认领） | 已实现 | s11 天亮 · 7:09.8–7:15.0 | “连接还在，不必重新认领” → 更换 Wi-Fi → 屿的家 | apk/DevicePage.kt（更换 Wi-Fi）<br>apk/SettingsPage.kt | 实现 + 模拟器；真机配网历史记录 |
| F07 | 本地唤醒“你好，openvela” | 已实现 | s05 我在 · 1:18.2–1:21.4 | 第一次说“你好，openvela”，本地唤醒 | app/bk7258_voice_kws.c<br>app/bk7258_agent_trigger.c<br>app/models/nihao_openvela.tflite | 实板：638 完整链路；11-03 真人唤醒两次本地命中；无真人多人统计 |
| F08 | 预置唤醒应答“我在” | 已实现 | s05 我在 · 1:20.8–1:21.4<br>s06 把她调成晴 · 2:23.4–2:31.0 | 预置“我在”（最新交付为授权板载同源 PCM 的 9 处后处理替换，非扬声器实录）<br>App：当前固件未提供独立的唤醒应答设置 | app/bk7258/bk7258_agent_trigger.c（bk7258_agent_trigger_reply）<br>app/bk7258/bk7258_agent_product.c（VOICE_CHANNEL_EVENT_WAKE_ACK_REQUEST）<br>app/bk7258/assets/WAKE_REPLY_PUBLIC.md<br>boards/bk7258/aidk_ai_toy/Kconfig（PUBLIC / PRIVATE / TONE 三选一）<br>apk/CustomizePage.kt | 实板：638 播放（验证配置使用私有授权素材）；旧版片中使用公开默认素材 wake_reply_public.pcm（Kokoro v1.1-zh sid 0，非克隆）。最新交付的 9 处“我在”替换为授权板载同源 PCM（SHA-256 772a8aa9…，该短 PCM 已按比赛期间授权入库）；两者均不是板子扬声器录音，也不构成新的实机验收 |
| F09 | 聆听 / 思考 / 说话状态眼 | 已实现 | s05 我在 · 1:20.6–1:29.7 | 唤醒 look_left → 思考 blink_half → 说话 speaking | app/bk7258/bk7258_agent_product.c（bk7258_display_activity / bk7258_display_speaking）<br>app/bk7258/bk7258_display_companion.inc（frames[]）<br>app/bk7258/bk7258_display_service.c | 实板：638/641；11-03 渲染回读 |
| F10 | 云端对话（ASR → Agent/LLM → 流式 TTS） | 已实现 | s05 我在 · 1:24.3–1:29.7<br>s05 我在 · 1:33.8–1:50.8 | 云端对话（片中回答为动画演绎）<br>插段：本地认出唤醒词，其余经 CP 出门到云端听/想/说，再流回来 | packages/ai_agent（官方 Agent，manifest 固定 76c65bc6）<br>app/bk7258_agent_product.c<br>app/bk7258_cloud_*.c<br>app/bk7258_agent_media_*.c | 实板：638 完整链路；11-03 夜间一次真人问答听到回答；775 在该次验证中未重做真人问答。后续 778/App 59 对话成功为用户确认，非重复声学或时延验收 |
| F11 | 免唤醒追问与静默回待机 | 已实现 | s05 我在 · 1:29.8–1:33.6<br>s07 日子 · 2:55.0–2:58.6 | 免唤醒追问<br>唤醒后不说话，静默回待机 | app/bk7258_agent_product.c<br>guide §B | 实板：638 |
| F12 | 半双工：播放中不能语音打断 | 已实现（有局限） | s07 日子 · 3:10.0–3:23.4 | 她播放时“好了好了”打断不了：半双工 | guide §B 第 4–5 条<br>app/bk7258_local_intent.c（BKLOCAL_STOP） | 产品边界（设计如此）；本地停止意图为主机测试 + 构建 |
| F13 | App“停止这次对话” | 实现未验收 | 不出现 | 实现了 App“停止这次对话”，但面向用户的取消没有实机验收；片中不演（半双工的局限由 F12 表现）。 | apk/DevicePage.kt<br>app/bk7258_agent_trigger.c（reply cancel 路径） | 实现；工程 HIL（701 固定响应）有取消记录；面向用户的播放中取消未实机验收 |
| F14 | 本地固定指令：音量、换表情、停止、专注 | 已实现 | s07 日子 · 3:24.0–3:25.4<br>s11 天亮 · 8:03.4–8:08.6 | 本地指令“结束对话”<br>本地指令“换成开心表情” | app/bk7258_local_intent.c<br>app/bk7258_focus_intent.c<br>app/bk7258_agent_local_intent.inc | 主机测试（run-agent-local-intent）+ AIDK 构建；新指令未实板验收 |
| F15 | 回答长度与回答模式 | 已实现 | s07 日子 · 3:26.4–3:31.4 | 回答长度 → 简洁；回答模式 → 快速对话 | apk/SettingsPage.kt<br>apk/MainActivity.kt（responseLengthLabel）<br>app/bk7258_agent_profile.inc | 主机测试；11-01 真机读写与重连 |
| F16 | 聊天风格（人格） | 已实现 | s06 把她调成晴 · 1:54.4–1:59.4<br>s11 天亮 · 7:38.6–7:41.0 | 聊天风格：轻轻嘴硬<br>聊天风格改为活泼俏皮 | apk/MainActivity.kt（directPersonas）<br>apk/DevicePage.kt（renderPersona） | 真机读写历史（9-19 实拍人格对话；11-01 读写） |
| F17 | 唤醒灵敏度与当前唤醒模型回读 | 已实现 | s07 日子 · 2:48.4–2:52.0 | 唤醒灵敏度 0.85 → 0.80，当前唤醒词回读 | apk/SettingsPage.kt（唤醒灵敏度）<br>apk/CustomizePage.kt（设备当前唤醒模型）<br>app/bk7258_voice_wake_package.c | 实现 + 真机历史；11-04 同机门限 85% 回读 |
| F18 | 唤醒模型导入 / 恢复上一模型 | 已实现 | 不出现 | 唤醒模型导入 / 恢复上一模型属于工程能力；片中只回读当前唤醒词（F17）。 | apk/CustomizePage.kt<br>app/bk7258_voice_wake_package.c | 实板：历史激活记录；11-04 同机私有模型恢复 |
| F19 | 扬声器音量（App / 按键 / 语音） | 已实现 | s07 日子 · 2:58.2–2:59.6 | 按两下 K1 调低音量 | apk/SettingsPage.kt<br>app/bk7258_agent_keys.c<br>app/bk7258_product_keys.h | 实板：638 音量键 |
| F20 | 拍照识物 | 已实现 | s07 日子 · 3:37.9–3:46.8 | “看看这个，这是什么？”拍一帧 640×480，云端识别回答 | app/bk7258_agent_vision.c<br>app/bk7258_vision_*.c | 实板：639 用户确认；有颜色识别错例，无准确率统计 |
| F21 | 回答表情 | 已实现 | s06 把她调成晴 · 2:10.2–2:11.9<br>s09 凌晨三点 · 5:22.6–5:26.6 | 回答表情：害羞<br>回答表情：难过 | app/bk7258_reply_expression.c | 主机测试 + 构建（11-01） |
| F22 | 动作联动表情 | 已实现 | s07 日子 · 3:53.6–3:57.9<br>s10 停电 · 6:23.4–6:24.6<br>s10 停电 · 6:42.3–6:43.2 | 拿起：look_left 800 ms；放下：blink_half 800 ms<br>断网时拿起：look_left（本地）<br>放到他手边：blink_half（本地） | app/bk7258/bk7258_agent_companion.inc（bk7258_display_companion(action, 800)）<br>app/bk7258/bk7258_display_companion.inc（frames[] = {0,3,1,4,…}）<br>app/bk7258/bk7258_motion_core.c | 实板：11-03 加速度样本持续更新；拿起与表情的关联未人工确认（作者粗剪有画面，版本未记录） |
| F23 | 完成触觉 | 已实现 | s07 日子 · 4:13.6–4:15.0<br>s12 灯塔 · 8:19.0–8:20.4<br>s12 灯塔 · 8:24.4–8:25.6 | 渲染完成：绿色对勾 + 40 ms 轻振<br>专注完成：对勾 + 轻振<br>最终渲染完成：对勾 + 轻振 | app/bk7258_haptic_*.c<br>app/bk7258_agent_companion.inc | 实板：11-03 驱动返回 0；实际振感未确认 |
| F24 | 按键：K2 软关机 / 开机 | 已实现 | s09 凌晨三点 · 5:32.8–5:37.6<br>s10 停电 · 5:58.9–6:01.1 | 按住 K2 三秒松手，软关机<br>停电后按 K2 开机 | app/bk7258_product_keys.h<br>app/bk7258_agent_product_power.inc | 实现；K2 关机 / 开机未单独验收 |
| F25 | 离线时的本地能力与如实失败 | 已实现 | s10 停电 · 6:00.1–6:49.1 | 断网：唤醒词无应答（不假装回答）；碰卡、拿起等本地反应照常 | README.md §日常使用（没有网络时）<br>v1103（当晚真人唤醒故障复现）<br>app/bk7258/bk7258_agent_trigger.c（trigger_process_locked） | 实板：11-03 缺配置时本地命中、ENETDOWN、不启动对话 |
| F26 | 专注计时 | 已实现 | s12 灯塔 · 8:13.8–8:20.4 | 碰卡开始专注 25:00（片中经剪辑），完成 | app/bk7258_focus*.c<br>app/bk7258_focus_pixels.h<br>app/bk7258_display_focus.inc | 模拟器 + 主机测试；完整实板计时未做 |
| F27 | NFC 场景卡 | 已实现 | s06 把她调成晴 · 2:31.0–2:40.4<br>s10 停电 · 6:14.3–6:16.3<br>s11 天亮 · 7:50.0–7:53.4<br>s12 灯塔 · 8:13.8–8:15.6 | 场景卡片登记门禁卡：播放四拍节律；碰卡<br>断网时碰卡<br>改登记为“开始专注”<br>碰卡开始专注 | app/bk7258_nfc_*.c<br>apk/MainActivity.kt（场景卡片） | 模拟器登记草稿；实卡登记与触发未实测（无卡扫描 -110） |
| F28 | 本地内容“四拍节律” | 已实现 | s06 把她调成晴 · 2:36.4–2:40.4<br>s10 停电 · 6:15.2–6:19.8 | 四拍节律（4 秒本地 PCM）<br>断网时四拍节律照常 | app/bk7258_local_content.c<br>app/assets/local_rhythm.pcm | 主机测试 8 个场景；实板播放未实测。片中声音即该 PCM 文件 |
| F29 | 签名原生小功能（帮我做个决定） | 产品固件禁用 | s06 把她调成晴 · 2:31.0–2:33.6 | “启动原生功能”在卡片操作里显示为不可用（产品固件已禁用） | app/bk7258_native_*.c<br>app/native-apps/shake_decide.c<br>boards/bk7258/aidk_ai_toy/configs/openvela_ap/defconfig（NATIVE_PUBLISHER_SHA256 为空） | 产品固件发布者指纹为空，正式固件拒绝安装；仅诊断配置下测试签名实板安装 |
| F30 | 跨重启记忆开关 | 已实现 | s06 把她调成晴 · 1:59.4–2:02.8 | 开启跨重启记忆 | app/bk7258_agent_memory*.c<br>apk/SettingsPage.kt（对话记忆） | 真机 Mi 10 + 实板：11-01 |
| F31 | 说出事实：称呼 / 偏好 | 已实现 | s06 把她调成晴 · 2:07.0–2:22.4 | “记住，我叫小鱼”“我喜欢加两勺糖的咖啡” | app/bk7258_memory_facts.c | 真机 + 实板：11-01 新增 / 纠正 / 删除 / 重启回读（虚构值） |
| F32 | 查看、纠正、删除事实 | 已实现 | s11 天亮 · 7:31.0–7:37.0 | 纠正称呼与偏好：小鱼 → 阿屿；不加糖的美式 | apk/SettingsPage.kt<br>apk/provision/AgentDataController.kt | 真机 + 实板：11-01 |
| F33 | 清空近期对话 / 删除设备记忆 | 已实现 | s11 天亮 · 7:37.0–7:38.6 | 清空近期对话 | apk/SettingsPage.kt | 主机 / 模拟器；11-01 真机删除路径 |
| F34 | Runtime Skill | 已实现 | s11 天亮 · 7:20.4–7:30.6 | Runtime Skill：设备体检 | app/bk7258_runtime_skills*.c<br>tools/ skill pack | 真机 + 实板：11-01 启停重启保持；635 安装 |
| F35 | 眼睛资源包导入、限时试用、设为默认 | 已实现 | s11 天亮 · 7:54.0–8:09.0 | 导入眼睛包、30 秒试用、设为默认 | app/bk7258_display_*<br>tools/_lib/workbench_resources.py<br>tools/_lib/workbench_trial.py<br>apk/CustomizePage.kt | 实板：641 两套资源跨复位保持；模拟器草稿 / 取消 / 默认选择；工作台正向实板未做 |
| F36 | 外观模式跟随系统 | 已实现 | s04 认领与配网 · 0:52.0–1:14.0<br>s11 天亮 · 7:09.8–7:53.4 | 夜里 App 为深色<br>早晨 App 为浅色 | apk/SettingsPage.kt（外观模式）<br>android/shaniu-companion/DESIGN.md | 模拟器两主题 |
| F37 | 电脑授权（手机一次性授予权限） | 已实现 | s07 日子 · 3:58.4–4:03.8 | 电脑 pair-start → App 导入、核对摘要、勾选资源管理/任务提醒/按需相机 → pair-finish | tools/_lib/workbench_pairing.py<br>app/bk7258_pc_authorization*.c<br>apk/MainActivity.kt（电脑授权） | 主机测试 + 模拟器文件确认 / 取消；手机→电脑完整实机纵向验收未做 |
| F38 | 电脑任务 task-run | 已实现 | s07 日子 · 4:03.8–4:15.0<br>s12 灯塔 · 8:20.2–8:25.6 | task-run 渲染 1–1024 帧，结果交给傻妞<br>task-run 最终渲染 2400 帧 | tools/_lib/workbench_task_runner.py<br>app/bk7258_pc_tasks.c<br>app/bk7258_focus_pixels.h | 实板：11-03 正常 0、超时 CLI 130，均 device_result=confirmed；屏幕与振感未人工确认 |
| F39 | 按需单帧取景（浏览器工作台） | 已实现 | s12 灯塔 · 8:46.0–8:53.8 | 工作台按需单帧取景，保存到本机 | tools/_lib/workbench_camera.py<br>tools/_lib/workbench_web/<br>app/bk7258_pc_camera.c | 实板：11-03 两帧 8,583 / 11,767 B，保存哈希一致，55 秒原速录屏 |
| F40 | 单次审批：批准 | 已实现 | s12 灯塔 · 8:34.0–8:46.0 | 批准归档命令：Bash: mv … /e/归档/，签名回执，执行完成 | tools/_lib/workbench_approval.py（approval-run / approval-hook）<br>app/bk7258_pc_approval.c<br>apk/provision/PcApprovalController.kt | 实板：11-04 0.7.129+775 一次批准，双眼感叹号、批准后清除（人工确认），返回 0 |
| F41 | 单次审批：拒绝 | 已实现 | s12 灯塔 · 8:25.4–8:34.0 | 拒绝删除命令：退出码 2，文件夹原样保留 | tools/_lib/workbench_approval.py<br>apk/provision/PcApprovalController.kt | 主机 C / Python / Android 测试；775 实板未复测 |
| F42 | 单次审批：过期 | 已实现 | s08 照片 · 4:44.0–5:12.2 | 删除请求无人决定，倒计时走完按拒绝处理，未执行 | app/bk7258_pc_approval.c<br>v1103（approval-expired.json） | 实板：11-03 无人批准返回 expired、process_returncode=null |
| F43 | 电脑上的资源作业 | 已实现 | s11 天亮 · 7:54.0–8:09.0 | 电脑工作台：导入、试用、设为默认（资源管理权限） | tools/_lib/workbench_resources.py<br>tools/_lib/workbench_catalog.py<br>tools/_lib/workbench_selection.py | 主机：HTTP → 真实客户端 TLS → 原生安装作业；11-03 未授权 profile 被拒 -13，正向实板未做 |
| F44 | 语音不能发起电脑操作 | 未实现 | s08 照片 · 4:44.0–4:49.8 | 命令由电脑上的 AI 助手请求；傻妞只提醒，手机决定 | README.md §设备、手机和电脑各做什么 | 产品边界（未实现） |
| F45 | 固件 OTA 与版本核对 | 已实现 | s07 日子 · 4:15.4–4:39.8 | 固件 OTA：下载、校验、重启、核对新版本 | apk/ota/OtaPackageServer.kt<br>apk/UpdatePage.kt<br>app/bk7258_agent_ota.c<br>app/bk7258_voice_ota_*.c | 实板：634 首次 OTA；767 升级与自动重连通过；770 升级成功但自动重连失败 |
| F46 | 签名启动链与 A/B 回退 | 已实现 | s07 日子 · 4:19.7–4:28.7 | 插段：B 槽写入签名镜像，A 槽保留，数据区不动，验签与回退 | boards/bk7258/<br>tools/_lib/image.py<br>tools/_lib/package.py<br>tools/_lib/trust.py | 多次实板 OTA 与整机烧录；CI 冷构建验签 |
| F47 | 转交或恢复出厂 | 已实现 | s09 凌晨三点 · 5:26.8–5:32.8 | 转交或恢复出厂（他退出了，没有确认） | apk/SettingsPage.kt（转交或恢复出厂）<br>app/bk7258_reset_main.c | 主机 / 模拟器；实际多介质清理与重新扫码未验收 |
| F48 | 移除本机连接资料 / 断开手机连接 | 已实现 | 不出现 | 移除本机连接资料 / 断开手机连接与故事无关；片中改 Wi-Fi 时连接保持（F06）。 | apk/DevicePage.kt | 实现 + 模拟器 |
| F49 | 三核分工：CP + AP SMP，两套 openvela | 已实现 | s05 我在 · 1:33.8–1:50.8 | 插段：CP（CPU0）与 AP（CPU1+CPU2 SMP）、RPMsg | chips/bk7258/<br>README.md §系统架构 | 多代实板；RPMsg 80 轮压力；CPU1 心跳停止（755/756/770）无共同根因 |
| F50 | 复用的 openvela 能力 | 已实现 | s05 我在 · 1:33.8–1:50.8<br>s13 在吗 · 9:28.2–9:38.0 | 插段标注 openvela、TFLite Micro、AI Agent<br>片尾致谢 openvela 组件 | contest2026_135_yongwangzhiqian.xml<br>SOURCE_PROVENANCE.md | manifest 固定提交；CI 冷构建 |
| F51 | Wi-Fi CSI 诊断采集 | 研究 | 不出现 | Wi-Fi CSI 诊断采集是研究功能，不在产品用户路径里。 | docs/verification/bk7258/2026-10-31-wifi-csi.md | 只有采集，没有存在 / 动作识别结论 |
| F52 | 三板复用（T5-Board Dolphin、T5AI-Core） | 平台 / 开发流程 | 不出现 | 三板复用（T5-Board Dolphin、T5AI-Core）是平台工作，片中只出现 AIDK 开发板。 | boards/bk7258/ | 三板 direct 构建；Dolphin 录音落卡用户确认 |
| F53 | 用自己的身份构建并刷入固件（开发板） | 平台 / 开发流程 | s03 做她 · 0:46.8–0:48.6 | 他用自己的身份构建固件（构建命令） | README.md（构建固件 / 用你自己的板）<br>docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md<br>tools/bk7258/bk7258.py（build） | 片中只展示构建命令与构建清单输出；下载与工厂初始化由剪辑略过，不代表片中时长 |

## 刻意不演的

- 不让傻妞用语音发起电脑操作（F44 未实现）：命令都由电脑上的人或 AI 助手发起，傻妞只提醒，手机决定。
- 断网时不让她“假装回答”，也不给唤醒加任何反应：固件在服务未就绪时消费本地命中，不播放“我在”、不改变双眼。
- 不演签名原生小功能（F29 产品固件禁用），卡片操作里只显示为不可用。
- 不出现指定唤醒词以外的唤醒词；不克隆、不模仿任何真实的人或前任的声音。

## 声音的来源

- “我在”：旧版工程使用项目公开内置素材 `app/bk7258/assets/wake_reply_public.pcm`（Kokoro-82M v1.1-zh，sid 0，非克隆）。最新交付的 9 处“我在”替换为授权板载同源 PCM（SHA-256 `772a8aa9…`，该短 PCM 已按比赛期间授权入库）；两者都不是板子扬声器录音，也不构成新的实机验收。
- 傻妞的其他台词：沿用原版 Kokoro 合成（0.8·sid0 + 0.2·sid8），与公开默认“我在”配套；不是从替换后的授权 PCM 克隆或重生成。真实设备上的回答来自用户配置的云端语音合成，音色可能不同。
- 周屿（旁白与对白）：剪映 SAMI 配音“温柔男主”（zh_male_iclvop_chenxuanqinggan），见 `audio/voice/audition/REPORT.md`。
