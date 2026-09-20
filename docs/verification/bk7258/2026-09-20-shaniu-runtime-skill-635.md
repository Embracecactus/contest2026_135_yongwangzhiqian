# 傻妞 635：运行时 Skill、语音与显示验证

日期：2026-09-20。物理操作者与语音/显示确认：用户；实现、构建和记录：CodeBuddy。
前述验证为用户确认；同日后续授权的 Codex 串口补验单列末节，不混用证据。

## 源码与产物

- 固件：`18.6.399+635`，AIDK AI Toy；技能安装和 `read_file` 由产品适配层接入。
- 产品入口 `bk7258_agent_product.c` SHA256：
  `bc974d72064fcbef096794db0dfe3a9616b4dfd86036bc16901f7b4beb91954f`；
  CMake 接线 `agent_framework.cmake` SHA256：
  `7448a637b2399bcecbaae9cb2d2f566de51146f188a798fa16b351c4faa310c0`。
  发布快照 `82610138` 与本机对应文件一致。
- MCUboot 构建 raw CP：1,107,276 B，SHA256
  `b4e35e14f088ee939b0f60ed9be2499cd6b0e3ea6bf467302b47a713a1fa80b0`；
  raw AP：1,604,600 B，SHA256
  `ea4d199211f94f3e973f7ff049a4d8b23017fefabd8738ac1c3d306770081bd6`。
  raw、签名/CRC Flash 段及完整包是不同产物，哈希不能混用。
- OTA 包：3,416,936 B，SHA256
  `05b7a741b1994ab4decf1dd015f52408334eb3331f20f927a1e06e62b55f5815`。
  已生成的 full 包 floor 为 635，OTA-only 包不更新 BL1/BL2；本摘要不根据包存在
  推断用户实际采用哪种写入方式。同板 full 恢复包和身份数据不公开分发。

## 用户提供的串口要点与结论

```text
bk7258: runtime skill installed: /data/agent/skills/device-assistant.md
[tools] Registered tool provider: bk7258-device
[tools] Registered tool provider: bk7258-camera
[tools] Tools JSON built (0 builtin + 2 providers)
Tools JSON loaded: 2012 bytes
BKVOICE official Trigger rearm ret=0
BKVOICE wake ready=1 result=0
BKDISPLAY RENDER PASS expression=neutral pack=shaniu-cyan-v2 revision=2 screens=2 fallback=0 sequence=1
AIDK DEFERRED DONE failures=0
```

用户与 CodeBuddy 确认运行时 Skill 安装及语音验证成功，双屏显示正常。
工具表大小证明注册内容，不单独作为模型读取技能正文的证明；语音通过结论来自
操作者确认。本轮没有取得独立多人唤醒泛化、长期压力或功耗测量结论。

来源为本机 `voice635-runtime-skill/hil-verify/boot-serial-notes.md`，SHA256
`e68a9a4f33fe9df940841d0d7c3cd3670431d9cc37e895b532a1fb54709becf8`。
它是用户日志要点与结论记录，不冒充完整原始串口文件。

## 验证边界与未闭合项

- 635 未重测 App 控制或 App OTA，沿用 634 的实际结果；相关行为代码未改不等于
  二进制相同，更不等于已重验。634 的 93% 断连、重启后 100%/版本计数确认见 Master Plan。
- 早期配置恢复 `-16` 后自动恢复、BT `0x2006/0x200a` 的 `0x0c` 为观察到的非阻塞项，
  未因为本次通过而宣称根因已修复。NFC 驱动适配在，未接入本次产品交互。
- 构建仍使用官方 Agent `e65550f` 加本地未提交扩展；本次不是公开 manifest
  干净复现。主仓发布与依赖公共仓提交/合入是分开的事项。

## 同日 Codex 串口补验

按新授权独占 COM8，执行一次 `reset reboot`，不使用 RTS/DTR，不重新烧录。
板端回读 `18.6.399+635 counter=635 pair=confirmed`；运行时 Skill 安装、
服务配置/TLS、KWS、阈值 0.60、持久 persona 与回答模式恢复，双屏状态
`READY last_error=0 pack=shaniu-cyan-v2 revision=2 fallback=0`。
运动传感器样本查询成功，马达入口 ready=1，电池电压 4116 mV；
电量百分比和校准温度仍 unavailable，不补造数值。supervisor faults/recoveries=0。

初始恢复 `-16` 后重试成功；BT `0x2006/0x200a status=0x0c` 仍存在。
本轮无真实声学对话/新照片/马达动作验收；ADB 未连接手机，未重测 App 或 OTA。
只读状态、KWS 持续推理与画面渲染日志不代替人工听看确认。

本机 `out/contest-hil-20260920/aidk-reset635/serial.raw` SHA256：
`856af1cba12df185c5e6c0bb2733c4049e17e4998a3e2600960d65793cb40b12`；
`aidk-peripherals/serial.raw` SHA256：
`b8208863d92fddec3f4e5b0aa7a2d5825542866153e4acb67f1f22977aafaa96`。
原始文件留本机，不公开身份、配置或个人环境信息。
