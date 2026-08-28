> **最后更新**：2026-08-10（Asia/Shanghai）
> **权威来源**：[CURRENT](../../../../progress/CURRENT.md)、[ROADMAP](../../../../progress/ROADMAP.md)
> **证据边界**：本页只解释当前仍在仓库中生效的实现；旧 N15/N17 文档是历史证据。

# 11 当前状态与下一步

## 1. 一句话结论

BK7258 已经形成完整的可恢复启动链：项目自己的 BL1 验证 Manifest，
NuttX MCUboot BL2 验证 CP/AP 两个签名镜像，并且只允许同一槽中的 CP/AP
成对启动。原来从零实现的 N15/N17 OTA 状态机已经删除，但 BL1、BL2 和
MCUboot 源码都完整保留。

## 2. 三层启动程序分别做什么

```mermaid
flowchart LR
    R[芯片 BootROM] --> B1[项目 BL1]
    B1 -->|验证 Manifest 和 BL2| B2[NuttX MCUboot BL2]
    B2 -->|验证同槽 CP/AP| C[CP NuttX]
    C -->|释放 AP| A[AP NuttX SMP]
```

| 层 | 当前职责 | 是否保留源码 |
|---|---|---|
| BootROM | 芯片上电后的不可修改入口 | 芯片内部闭源 |
| BL1 | 时钟、复位、看门狗、Manifest 验签、主/备 BL2 回退、SRAM 搬运与跳转 | 完整保留 |
| BL2 | 使用固定版本的 NuttX MCUboot 校验镜像头、TLV、签名、版本和安全计数；保证 CP/AP 来自同一槽 | 完整保留 |
| CP/AP NuttX | 运行系统、驱动、网络、蓝牙和应用 | 完整保留 |

所以“简化启动链”的含义是减少重复的 OTA 策略层，不是把 BL1 或 BL2
改成空壳，更不是删除 MCUboot。

## 3. 删除的旧 OTA 是什么

旧实现自行定义了以下机制：

- OTA metadata 和双 bank 日志；
- staging、publish、trial、confirm、rollback 状态机；
- N17 release key、format-3 journal 和 policy sector；
- `bkota` 命令及配套 host harness、campaign、布局/故障验证脚本。

这些机制曾完成 N15 的 A→B→A 实板验证，因此对应 ADR、日志和 verification
记录仍被保留。但是它们与 MCUboot 自带的 slot、image trailer、confirm 和
rollback 语义重复，继续维护会形成两套真相源，所以现役代码已经删除。

## 4. 当前 Flash 布局

```text
0x000000  BL1
0x011000  Primary CP
0x165000  Primary AP
0x286000  Secondary CP/AP pair (s_app)
0x4fc000  usr_config
0x50b000  Primary BL1 Manifest（只读）
0x50c000  Secondary BL1 Manifest（只读）
0x51d000  Primary BL2（只读）
           Secondary BL2 紧随其后
0x600000  LittleFS，大小 1 MiB
0x7fa000  official calibration tail
0x800000  Flash 结束
```

布局仍保留 A/B 成对空间，因为 MCUboot 和以后可能实现的更新器都需要
inactive slot。被删除的是自定义 metadata/auth 分区和运行时写入逻辑，
不是删除 Secondary slot。

## 5. 这次如何确认没有删坏

使用 official BK7258 SDK v3.1.1.9 和仓库固定的 NuttX MCUboot，完成了
32 线程全链构建：

1. 生成并校验 CSV 分区表；
2. 编译 Manifest 强制模式 BL1；
3. 编译 NuttX MCUboot BL2；
4. 编译 CP 和 AP；
5. 用临时开发密钥给 CP/AP 签名；
6. 做 BK7258 32+2 CRC 编码；
7. 生成最终 factory package 并通过布局检查。

这证明源码和打包链仍然闭合。临时私钥位于 `/tmp`，没有写进仓库。
本次是构建/host 验证；新的清理后镜像还需在下一次安全硬件检查点下载并
复验 Primary、Secondary fallback 和 CP/AP 启动。

## 6. 当前明确没有什么

- 没有 field OTA 下载协议；
- 没有 inactive-slot Flash writer；
- 没有运行时 confirm/rollback 服务；
- 没有 OTP/eFuse 根密钥和硬件防回滚；
- 没有声称 BK7258 BootROM 会读取项目自定义 Manifest。

因此当前准确说法是“具有完整的软件根签名启动链”，不是“已经具有完整的
产品 OTA 和硬件 Secure Boot”。

## 7. 下一步

1. 把本次 OTA 清理作为独立提交审查，避免混入同时存在的驱动修改。
2. 在可安全操作硬件时下载清理后的镜像，复验主 BL2、损坏主 Manifest 后
   的备 BL2 回退，以及 CP/AP 正常启动。
3. 真正开始 field OTA 时，先评审当前 NuttX MCUboot 的 upgrade、confirm、
   rollback 和 image trailer 接口，再只补 BK7258 必需的 Flash/CP-AP 成对
   wrapper；不恢复旧 N15/N17 journal。
4. OTP/eFuse 和 Secure Boot lifecycle 仍是最后且单独授权的阶段。
