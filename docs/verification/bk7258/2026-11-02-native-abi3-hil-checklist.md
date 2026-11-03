# 2026-11-02 原生功能 ABI 3 实板验证清单（待执行）

本文件是交给实板操作方的检查清单，不是验收记录。实板结果请另建日期化记录，
状态汇总仍只写入 `boards/bk7258/CONFIGS.md`。行为合同见
[桌面伙伴合同 N5](../../platforms/bk7258/shaniu-desktop-companion.md#n5-语音启动定时与保存进度abi-3)。

## 开发侧已完成的检查

| 检查 | 结果 |
|---|---|
| `make -C tests/host/bk7258 native_apps` | 通过：ABI 1/2 回归、ABI 3 结果/时钟/保存进度/定时/目录 |
| `make -C tests/host/bk7258 run-native-intent` | 通过：意图种类与窗口、目录快照、本地语句、动作 7 |
| `python3 tests/host/bk7258/test_native_app_package.py` | 7/7 |
| NFC/本地内容/companion 相关 58 个回归用例、`test_local_content.py`、`test_shaniu_local_intent` | 通过 |
| Android `NativeFeatureStoreTest` 7、`NfcBindingControllerTest` 19、`AgentDataControllerTest` 11 | 通过 |
| `bk7258.py build --board aidk_ai_toy --boot direct` | 通过，改动文件零警告；仅证明可编译 |
| `gcc -fanalyzer` 于 `bk7258_native_apps.c`、`bk7258_native_intent.c` | 无告警 |
| 三个 ABI 3 示例交叉编译 | 均无外部符号，约 1.5 KB，仅 `R_ARM_ABS32` |

## 前置条件

1. 按现行 SOP 构建并下载正常 `openvela_ap` 候选（正式签名链），发布者指纹沿用
   上一轮实板所用的已批准配置；不要使用本清单生成任何密钥。
2. 设备已认领、控制已绑定、Wi-Fi 可用；`clock_valid` 需为 true（网络时间或手机
   下发时间，6 小时内）。
3. 手机安装本轮 Android 构建（含 ABI 3、定时显示与 8191 B 回读上限）。
4. 用已批准签名打包（版本号须大于设备上已有同 ID 版本）：

```bash
python3 tools/bk7258/bk7258.py native-app pack --source app/bk7258/native-apps/shake_decide.c \
  --id shake --name 摇一摇做决定 --description 摇一摇给出去或不去 --version <N> \
  --abi 3 --permissions display,motion,haptic --results 去吧,再想想 \
  --signing-key <approved.pem> --output <out>/shake
python3 tools/bk7258/bk7258.py native-app pack --source app/bk7258/native-apps/nudge.c \
  --id nudge --name 喝水提醒 --description 到点提醒，拿起即完成 --version 1 \
  --abi 3 --permissions display,motion,haptic,clock,storage --results 完成,错过 \
  --signing-key <approved.pem> --output <out>/nudge
python3 tools/bk7258/bk7258.py native-app pack --source app/bk7258/native-apps/sit_reminder.c \
  --id sit --name 久坐提醒 --description 久坐不动时提醒起身 --version 1 \
  --abi 3 --permissions display,motion,haptic --results 起来了,还没动,一直在活动 \
  --signing-key <approved.pem> --output <out>/sit
```

记录每个包的 `image_sha256`、`package_size`、`app_hash`。

## 用例

每项保留串口原文与手机截图。“日志”列为应出现的关键行。

| ID | 步骤 | 预期 / 日志 |
|---|---|---|
| R1 | 回归：已装 ABI 1/2 功能从手机启动、停止；NFC 动作 7 卡片启动 | 行为同上一轮；`BKNATIVE intent=… kind=1 result=0` |
| I1 | 手机安装三个 ABI 3 包 | 确认框列出权限中文名；回读 `abi:3`、`results` 标签；`abi_max:3` |
| I2 | 安装一个 ABI 3 包后卸载再装同版本 | 卸载成功；重新安装成功（新记录） |
| V1 | 说“傻妞，帮我做个决定” | `BKLOCAL … kind=app result=0 … llm=0 tts=0`；交互结束后 `BKNATIVE intent=N kind=1 result=0`；思考表情 |
| V2 | V1 后摇动设备 | 开心或中性表情约 3 s + 一次短振动（受 2 s 冷却）；随后结束；手机显示“上次结果（shake）：去吧/再想想” |
| V3 | V1 后不动直到超时（默认 60 s） | 结束，`last_value` 为 0，无结果标签 |
| V4 | 说“傻妞，打开喝水提醒” | 本地语句命中 nudge；交互结束后启动 |
| V5 | 说“傻妞，启动摇一摇做决定，给我二十秒” | 走 LLM：`BKVOICE device tool=device_app result=0`；20 s 等待窗口 |
| V6 | 说“傻妞，刚才的结果是什么” | 工具 `list` 返回 `last_result` 标签，回答与 V2 一致；不得在启动当轮编造结果 |
| V7 | App 运行时说“傻妞，停止喝水提醒” | 语音开始即中止 App（`last_result` -125）；工具返回 `not_running`，回答不报错 |
| V8 | 说“傻妞，不要打开喝水提醒” / “能帮我做个决定吗？” | 不命中本地语句，交由 Agent；不得启动 App |
| C1 | 手机回读 | `clock_valid:true`；断网超过 6 小时或未同步时为 false |
| S1 | 说“傻妞，每天 HH:MM 提醒我喝水”（取 2–3 分钟后） | `device_app schedule` 成功；手机显示该定时“每天 喝水提醒 · 尚未到时” |
| S2 | 到点保持安静 | `BKNATIVE schedule slot=… minute=… result=0`；开心表情 + 振动，每 20 s 至多 3 次；拿起即结束，结果“完成” |
| S3 | 再设一条 2 分钟后的定时，到点那一整分钟持续说话 | 记录一次 `result=-16`；之后不补发；手机显示“上次已跳过” |
| S4 | 设定后复位设备 | 定时仍在；到点仍能触发（触发记录不跨重启） |
| S5 | 核对时区 | 触发时刻与北京时间一致 |
| S6 | 说“傻妞，取消喝水提醒的定时” | `unschedule` 成功；回读不再显示 |
| P1 | nudge 两次完成；若有 nsh，`ls /cpdata/shaniu/native-apps/state-nudge` | 存在 `config.bin`；复位后再完成仍成功 |
| P2 | 卸载 nudge | `state-nudge` 目录被删除 |
| T1 | 说“傻妞，启动久坐提醒，六十秒”，之后保持设备静止 | 约 60 s 后开心表情 + 振动；移动后结束，结果“起来了” |
| T2 | 观察全程日志 | 无 `-110`（单步超时）、无 supervisor fault/recovery 增加 |
| B1 | 四个功能 + 定时全部装满后手机回读 | 回读成功（≤ 8191 B），无 `-28` |

## 判定与边界

- 主机测试不能证明动作识别率、振动体感、语音交互时序或时间同步可靠性，以上
  均以实板为准。
- App 不是后台服务：任何语音交互、OTA、专注/任务界面都会中止正在运行的 App，
  久坐提醒因此可能被中断，属合同内行为。
- 设备无主动语音播报；结果只在表情、手机与下一轮工具查询中可见。
