# 傻妞 635：运行时 Skill、语音与显示验证

日期：2026-09-20。物理操作者与语音/显示确认：用户；实现、构建和记录：CodeBuddy。
本文是已完成验证的公开摘要，不是 Codex 重新操作硬件的记录。

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
