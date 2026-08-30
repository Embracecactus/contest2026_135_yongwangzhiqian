# BK7258 N14 PSRAM / timer 最终证据索引

> 日期：2026-08-03
> 状态：**COMPLETED / `board-verified`**
> 对应 N14 PSRAM Stage 计划已归档；本文件保留可复核的证据索引。
> 源码复核：[n14-psram-source-verification.md](n14-psram-source-verification.md)

## 1. Frozen build baseline

| 项 | 最终值 |
|---|---|
| branch | `feat/bk7258-n14-psram` |
| base commit | `c6afd6f9b73dcf862f17bd31f5b2dc90820b9bb0` |
| CP config | `cp_nsh_psram` |
| AP config | `ap_smp_psram` |
| SDK bundle | `v3.1.1.9` |
| detected PSRAM | APS128XXO，`id=0x8d08`、`config=0x8d1a`、16 MiB |
| CP heap | `[0x60700000,0x60720000)`，128 KiB |
| AP heap | `[0x60720000,0x607c0000)`，640 KiB |
| AP reserved section | `[0x607c0000,0x60800000)`，256 KiB |
| upper physical half | `[0x60800000,0x61000000)`，boot-tested/reserved |
| cache policy | MPU region 6，normal non-shareable non-cacheable |

最终 clean full dual build退出0，PSRAM/RPTUN/BLE/dual-image门禁全部PASS。官方NuttX/apps/SDK
source与SDK archive均未修改。

## 2. Final artifact hashes

| artifact | SHA-256 |
|---|---|
| CP `app.bin` | `6a0c2fc2cdf48c61994d5d40e148c0dadb3f72ace5240cf71e0dff978d97cf49` |
| CP `app_crc.bin` | `80f17f3c0fe31ccd5b0c1e9519b9a0054d43e389acb618c922df34c34ed67e2a` |
| CP padded flash segment | `a6aec2c05c7cc28f93bf3c12558a71e34a94ad89f1d748c715e83dc2b2dac374` |
| AP `app1.bin` | `9e95bbb7f9cfc8d4302929bc18eeb81ca85f23accb7e087b1d238a8afad259ce` |
| AP `app1_crc.bin` | `8e9db11fc44620ca7a73e82031a984e584dc7d6a507f29f4e3a6c3152cab587d` |
| AP padded flash segment | `bc9087fd135ad0f62b53aed11e53f9d49eaa738ccbf044b8d5ee4e0532b30cfe` |
| `all-app-factory.bin` | `3b34edc5d86343dcb0a3f479d71eb1271c49157eb93c51b5bb6da13fafcef253` |
| `bk7258-psram.json` | `c4e486b4b921e3a1a78b3362f639600cd879f09d887e43002aebf173e514a1bb` |
| `bk7258-rptun-layout.json` | `ed4319db1c1093ee437f73f6c5292f35a31bd67bca3bc1caa5aa96d0be23dc54` |
| `bk7258-ble-gatt.json` | `255962ea5f1107416c5c52ab8406b91f9d4933b4a54c3225eb5efa14ce088fa6` |
| `bk7258-dual-image.json` | `e89610b569bacee13b2a3400c589f3c0c7b9e9d02b4b4830ee5367334916a78e` |

SDK role manifests：CP
`438c1bf16a37cbfe13adda7e7e99c5f757c82d7b6cc04d61521ca1836155c7be`，AP
`5d4b7908fd21201a5f5ec3537915209aaed0273dc9779d8ba72a40ab82056edc`。

## 3. Primary functional gates

| Gate | 实板结果 | raw evidence | raw SHA-256 |
|---|---|---|---|
| initial PSRAM info | raw capacity `1/1`；AP CPU0/CPU1 `16/16`，error 0，free稳定 | [psram-info.raw](../../../../logs/bk7258-n14/20260803-psram-spinlock/hardware/psram-info.raw) | `212d7124e0df8e6c4bba91d15c1f24805aba6c08bf89223d03b5228ae27bafd8` |
| AP warm cycle | `apctl cycle 10 60000`，generation 2..11全部PASS | [ap-cycle-10.raw](../../../../logs/bk7258-n14/20260803-psram-spinlock/hardware/ap-cycle-10.raw) | `3ec9196c731e144641563a6307c7f632c8389b0a3d539bd4568be06b718f2ca1` |
| post-cycle restart | generation 12 READY后PSRAM再次`16/16` | [psram-info-generation12.raw](../../../../logs/bk7258-n14/20260803-psram-spinlock/hardware/psram-info-generation12.raw) | `6a820cb1435b6c59e374fc8c8336d1e1285c855c9496359affaa39591d6da7d8` |
| SDK timer | `bktimertest 256`，callbacks256、20 ms callback、queued delete1 | [timer-256.raw](../../../../logs/bk7258-n14/20260803-psram-spinlock/hardware/timer-256.raw) | `a83c099f31171e4e603c8b1ceb36f703b8fca2b011de9d7cf26abbc3f2f450e5` |
| CP heap | `bkpsramtest all 256`，256/256、free `131056→131056` | [psram-all-256.raw](../../../../logs/bk7258-n14/20260803-psram-spinlock/hardware/psram-all-256.raw) | `4bc169fc5e7cbee2bfa907daeeee1ab39365dae0d635ec076067e758726765b6` |
| RPMsg regression | 六场景，CPU0/CPU1均100/100、error0、heap稳定 | [rpmsg-all-100.raw](../../../../logs/bk7258-n14/20260803-psram-spinlock/hardware/rpmsg-all-100.raw) | `0bafb229604892152851073d3fc3b575d6840e6670e9086df9eea43687bdc4e7` |
| Bluetooth regression | info PASS、真实BD_ADDR、fallback0、ACL MTU70/buffers20 | [bt-info.raw](../../../../logs/bk7258-n14/20260803-psram-spinlock/hardware/bt-info.raw) | `d8a14d24f91fe9a1d71767cc1562c721776de90e0c305ea150075fa8cb8bf3b9` |

稳定输出锚点：

```text
BPSR INFO status=0 ready=1 id=8d08 config=8d1a capacity=16777216
BPSR RAW runs=1 passes=1
BPSR APTEST status=0 requested=16 completed=16/16 active=16/16
  stage=12/12 errors=0/0 cpu=0/1 free=655344->655344
BPSR INFO PASS
```

## 4. Physical cold and final clean image

outer-spinlock候选完成三次独立RTS physical reset，均`cold_path=yes`、`PASS_NSH`，随后每轮
`bkpsramtest info`均回到generation 1、raw `1/1`、AP `16/16`：

1. [round 1 summary](../../../../logs/bk7258-n14/20260803-psram-spinlock/physical-reset-1/20260803-172133/summary.txt)
2. [round 2 summary](../../../../logs/bk7258-n14/20260803-psram-spinlock/physical-reset-2/20260803-172323/summary.txt)
3. [round 3 summary](../../../../logs/bk7258-n14/20260803-psram-spinlock/physical-reset-3/20260803-172509/summary.txt)

移除临时HardFault callee-R4 telemetry并清理注释后，又执行一次clean full build与稀疏烧录：

- [final sparse-flash summary](../../../../logs/bk7258-n14/20260803-final-clean/20260803-173408/summary.txt)：`PASS_NSH`
- [final physical-cold summary](../../../../logs/bk7258-n14/20260803-final-clean/cold/20260803-173529/summary.txt)：
  `cold_path=yes`、`PASS_NSH`
- [final PSRAM info](../../../../logs/bk7258-n14/20260803-final-clean/final-psram-info.raw)：16 MiB、raw1/1、AP16/16、free稳定

## 5. Factory first-calibration gate

最终factory镜像执行了一次明确授权的完整写入，覆盖/清空旧LittleFS与校准区，以验证真正的首次
PHY/RF calibration启动顺序。它不是稀疏升级操作；需要保留数据时仍应使用三段稀疏烧录。

- [factory flash/boot summary](../../../../logs/bk7258-n14/20260803-final-factory-calibration/20260803-173742/summary.txt)：
  factory write PASS、`PASS_NSH`
- [factory PSRAM info](../../../../logs/bk7258-n14/20260803-final-factory-calibration/factory-psram-info.raw)：
  16 MiB、raw1/1、AP16/16、free稳定
- [factory AP status](../../../../logs/bk7258-n14/20260803-final-factory-calibration/factory-ap-status.raw)：
  AP READY、RPTUN CONNECTED、supervisor HEALTHY、CPU2 online、SMP gates PASS、pending0/0
- [factory Bluetooth info](../../../../logs/bk7258-n14/20260803-final-factory-calibration/factory-bt-info.raw)：PASS
- [post-calibration cold summary](../../../../logs/bk7258-n14/20260803-final-factory-calibration/post-calibration-cold/20260803-174150/summary.txt)：
  `cold_path=yes`、`PASS_NSH`
- [post-calibration PSRAM info](../../../../logs/bk7258-n14/20260803-final-factory-calibration/post-calibration-psram-info.raw)：PASS

## 6. Closure decision

N14满足 source、build、ELF ownership、实板功能、AP SMP并发、warm lifecycle、physical cold、factory
首次校准及既有RPMsg/Bluetooth回归门禁，状态升级为`board-verified`。

未由本结论覆盖：upper 8 MiB runtime allocator、cacheable/DMA PSRAM、动态CP/AP分区、长期性能/温压
stress、物理断电以及下一MAIN Stage功能。任何这些扩展都需要新的计划和独立证据，不能复用N14
功能PASS直接宣称完成。
