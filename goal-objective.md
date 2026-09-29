# 傻妞：工厂工程验证闭环

日期：2026-09-29
执行基线：主仓 `dad2df50cace7c4e7eb8b9b1cb3513382df45dfe`；Agent `7f5fde721a0698e1e91ce0305f2097abc314e0d4`。

本目标替代此前暂停和保留设备状态的安排。冻结需求、历史 Red/Green、夹具与输入哈希仍按原证据文件保存，不改写归属。

## 当前目标

生成匹配当前源码的 factory engineering 全镜像，经现有 HIL 对已连接的 AIDK 板执行一次正式 factory-init 全量烧录，验证首次启动、临时工厂诊断授权、BKTEST K2 状态机和固定 PCM 的 Audio/EOF/取消/下一轮路径。

## 权限与边界

- 已授权本轮 8 MiB 工厂全量烧录、factory-init、必要复位、启动日志和无人值守板端测试。
- 用户数据可按正式 factory-init 清理；不写 OTP/eFuse，不更换 root key，不覆盖校准、设备唯一身份或其他 protected/immutable 区域。
- 手机不可用；不等待扫码、BLE、热点、实体 K2、拔插、听音、NFC 或人工观察。
- 工程诊断入口必须默认从 production 构建移除，仅允许工厂未认领状态通过现有 `bkprov-v1` 物理 HIL 通道装入一次性 RAM 主体，再经原生 USB TLS/SDC1 使用；超时、撤销、重启或认领后失效，不写 PCG1/owner。
- K2 测试进入现役虚拟按键事件、按键接收器、power coordinator 和有界 CP peer，不直调 shutdown/reset。
- Audio 使用固定低幅 PCM 和现役 Agent `audio_playback`/Media 所有权；无声学回采时只报告数字与 Media 证据。

## 当前证据

- 板上预烧录版本：`0.7.46+691`，A 槽 confirmed，CP online、RPMsg ready、supervisor faults/recoveries 为 0。
- 预烧录证据：`../out/shaniu-factory-bktest-20260929/preflash/`。
- 同板 accepted base：`../out/shaniu-board-20260922/relocated-base.bin`，8 MiB，SHA256 `cfbf8d2a3c7f8125d973db2401d84d727ce501e41f9857feaf639c333f967a63`；证据 `accepted-base.json` 绑定当前布局 `bk7258-510173147382a879` 和设备 `C8:47:8C:CB:7F:80`。
- 当前 BKTEST 主机路径已通过，但板端命令因无 diagnostics grant/profile 而 fail-closed；这是本轮首先关闭的认证缺口。

## 执行顺序

1. 冻结工厂临时诊断主体和 Audio 入口合同；在旧实现记录 `BLOCKED_INTERFACE`/既有主机基线，不伪造业务 Red。
2. 实现现有 `bkprov-v1` 工厂通道的工程专用 RAM 诊断主体、原生 USB TLS/SDC1 profile 建立与撤销；production 保持关闭。
3. 完成 K2 2999/3000/3001、无 held、CP reject/pending/timeout/late ACK、重复与失效会话；完成固定 PCM EOF/cancel/next/drain。
4. 运行受影响测试及完整门禁，构建并签名 CP/AP/BL/resource/APK 交付集合，生成含 factory-init 的 8 MiB 同板镜像及哈希清单。
5. 释放端口，使用既有 BK Loader 全量烧录；采集完整首次启动，核对 BL/CP/AP、服务、默认资源和实际源码身份。
6. 建立临时 profile，执行 BKTEST，撤销并验证旧 profile 失败；生成 factory validation report。

## 验收记账

分别记录 `PASS`、`BLOCKED`、`NOT RUN`。下载成功不等于启动，BOOT PASS 不等于服务就绪，主机测试不等于板端，软件 K2 不等于实体按键，Media 完成不等于真实听感。实体 K2、深睡功耗、手机 BLE/App OTA、扫码认领与声学质量保留待现场层。

平台 goal 工具仍保存旧 paused objective，且当前接口不能合法替换未完成目标；本文件与用户 2026-09-29 最新 `/goal` 是本轮执行依据。
