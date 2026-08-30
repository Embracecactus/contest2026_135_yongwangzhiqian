# BK7258 现役 chip 主机回归夹具迁移

- 日期：2026-08-27
- 分支：`feat/bk7258-p0-xts-completion`
- 基线提交：`ecc1c0a185896d6afce165d20ebbf1a270782683`
- 结果：**PASS（主机源码/ABI 门禁）**
- 硬件边界：未连接、下载或修改 T5-Board；不构成板端 xTS PASS

## 1. 初始失败与根因

初始执行 `./tests/bk7258/run_tests.sh` 在任何有效测试前失败：Makefile 仍从已退役的
`../../board/bk7258/...` 取源，并继续构建已经删除的
`bk7258_bl2_keys.c`、`bk7258_bl2_mcuboot_boot.c` 和旧 BL1 manifest API。原脚本还只
执行五个公共二进制，即使 `make all` 构建了 BL1/BL2/AP，也不会运行它们。因此此前
不能把该入口记为 PASS。

## 2. 迁移后的来源与生成契约

- 所有真实实现改从 `chips/bk7258/{common,bootloader,ap,cp,include}` 编译；
- 主机分区头由当前 Agent 权威 CSV 临时生成到 `tests/bk7258/build/layout/`；
- BL1 测试改用现役 `bk7258_boot_flash_read()` 和
  `bk7258_bl1_manifest_verify_buffer()`；
- BL1 使用确定性的公钥-only fixture 模拟生成源码符号，不包含私钥，也不作为可部署
  信任根；
- 删除固定开发公钥的 BL2 keys 测试。正式 MCUboot key 是每次 build 生成物，不允许
  被主机测试钉死；
- 删除已不存在的旧 BL2 handoff 测试，新增现役 `bk7258_bl2_pair_order()` 的主/备、
  版本字段和同版本 factory-primary 测试；
- BL2 flash-map 测试更新为当前语义：只有可见 slot 的 `copy_done` 可写、按 32+2
  CRC 重编码并做 sector RMW，整 slot erase 必须满足严格参数；
- `run_tests.sh` 统一运行 core、BL1、BL2、AP 全部分层目标，并输出可机器识别的
  BEGIN/PASS 标志。

## 3. 执行中发现并修复的真实问题

CAN 回归暴露出两个不同层次的问题：

1. 现役 chip driver 收到 DLC 大于 8 的损坏记录时只清空本地 header 状态，没有消费
   SDK 字节流中该记录剩余的 payload。下一次接收会把 payload 当成新 header，造成
   永久失步。`bk7258_can_drain_one()` 现通过 `rx_discard_len` 分块丢弃损坏记录，并在
   reset/setup 时清零该状态。
2. 主机 kthread mock 把 pthread detach 后让 `kthread_delete()` 立即返回，旧接收线程
   可跨测试继续访问单例。fixture 现初始化完整 mutex/condition，并在删除路径真实
   `pthread_join()`。

旧测试还把 CAN ID 0、DLC 0 的数据帧判成非法；经典 CAN 允许零长度数据帧，断言已
改为先完成前一帧，再验证零长度帧成功提交。JPEG parser 的 component ID 数组增加
防御性零初始化，消除编译器无法推导 `sof_seen` 与赋值关系产生的未初始化告警。

## 4. 可复现实验环境

完整命令：

```bash
./tests/bk7258/run_tests.sh
```

本轮脚本记录：

| 项目 | 值 |
|---|---|
| 基线 Git | `ecc1c0a185896d6afce165d20ebbf1a270782683`（验证时含本分支工作树修改） |
| C 编译器 | Ubuntu `cc 11.4.0` |
| Python | `3.10.12` |
| cmocka | `1.1.5` |
| sanitizer | `test_boot_bl1_policy` 使用 ASan + UBSan |
| 分区 CSV SHA-256 | `8c66c2f3d10db658d464580dfce3ea339efc7c2f2c9dc59d4a87ab8013e728cc` |

结果摘要：

| 层 | 执行结果 |
|---|---|
| core | mailbox 31 checks、RPTUN CP 47 checks、RPTUN AP 32 checks、PM activity 与 BL1 policy 均 PASS |
| BL1 cmocka | 42/42 PASS |
| BL2 cmocka | 42/42 PASS |
| AP/CP 外设 cmocka | 197/197 PASS |
| cmocka 合计 | 281/281 PASS |
| 完整入口 | exit 0，最终标志 `BK7258_HOST_TEST_PASS` |

`python3 -m py_compile`（layout generator 与 patch helper）和
`git diff --check` 同时 PASS。

## 5. 未覆盖与后续门禁

- 后续 generation 149 已完成当前代非破坏性板端核心 xTS：mm 8/8、sched 16/16、
  ostest 状态 0、getprime、mm/ramtest、scanftest 164/0、hello/pipe 和最终冷启动；见
  [generation 149 xTS 记录](2026-08-27-bk7258-p0-xts-completion.md)；
- GPIO/UART 物理 loopback、AP RTC/timer/RNG、`driver_test`、LIBCXX 和受控 fault 仍需
  夹具或隔离 profile 后独立执行；
- fstest/blktest 只能使用专用可恢复介质，不能写 Agent persistent 或用户 TF 数据；
- opus_ramtest 需要独立 RAM 预算与压力 profile；
- `mock_boot_flash_program()` 以 `memcpy` 模拟写入，没有模拟真实 NOR 只能从 1 写到 0
  的电气约束；flash-map 的参数、sector RMW 和 CRC 行为已覆盖，但物理写入限制仍由
  bootloader 实现审查、签名包门禁和实板验证承担；
- 12 小时 soak 经仓库 owner 于 2026-08-27 确认延期，本轮不作为阻塞门禁；
- 主机 PASS 不升级任何尚未实板复核的官网文档项。
