# 傻妞工厂部署指南（评委可复现）

目标：从公开产物得到一台“自主首启 + 屏幕扫码认领”的傻妞。工厂阶段允许使用
电脑与串口；**部署完成并断开电脑后，首次使用只允许手机操作**。

## 1. 先决条件

- 公开产物：通用固件载荷（本仓库 `main` 源码可复现构建）、工厂部署工具
  `tools/bk7258/bk7258.py`、布局与发布策略 CSV、APK
  `shaniu-companion-0.5.24-shaniu-firstuse-debug.apk`、内置基础资源
  （固件 ROMFS 内的唤醒应答音、内置唤醒模型；眼睛包/唤醒词包可选）。
- 8 MiB 整片镜像必须由**目标板自身**的读回构成基底，保留
  `usr_config`（除前 8 KiB 工厂记录）、`easyflash`、`easyflash_ap`、`sys_rf`、
  `sys_net` 与不可重建的校准/身份尾区；不得把别的整机镜像复制过来。
- 不需要作者的私钥或设备数据。

## 2. 构建

```bash
tools/bk7258/bk7258.py build --board aidk_ai_toy --boot direct
```

`--boot direct` 是无签名诊断链（不能发布）。构建产物在
`out/bk7258/aidk_ai_toy/<...>/releases/direct/images/{boot,cp,ap,pair}.bin`，
offsets 由生成的 `bk7258_partitions.h` 给出（boot 0x0、cp 0x11000、ap 0x143000、
pair 0x352000）。

## 3. 组成工厂整片镜像

1. 读回目标板整片 8 MiB（loader `read`），它是唯一允许的基底来源。
2. 用上述四个 direct 段覆盖 `boot/cp/ap/pair` 对应范围。
3. 把 `usr_config` 的前 8 KiB 擦成 `0xff`（无历史工厂事务），
   把 `persistent_data`（1 MiB @0x6e8000）擦成 `0xff`：用户存储必须由
   **设备自己**在首启时初始化，不要预置离线 LittleFS 镜像。
4. 记录镜像 SHA256。

## 4. 烧录

```bash
python3 <skill>/scripts/bk7258_hil_download.py preflight \
  --board aidk_ai_toy --transport single --artifact-kind direct-full \
  --loader <bk_loader.exe> --port <COM> --image <整片.bin> \
  --expected-size 8388608 --expected-sha256 <sha256>
python3 <skill>/scripts/bk7258_hil_download.py run ... --execute \
  --evidence-dir <new-directory>
```

AIDK 的 CH340 不接 CEN/RST：只使用 loader 自带的
`--swrst "reset reboot" --hard-reset 0 --reboot 1 --uart-type CH340 --fast-link 1`
原子重启窗口，不要用 RTS/DTR 或延时串口复位。loader 退出码仅供参考：
`FLASH_PASS` 需要日志中的成功 marker。

## 5. 建立工厂事务（正式授权）

串口控制台（CP），在设备启动到 NSH 后：

```text
nsh> bkfactory status
BKFACTORY STATUS state=empty ...
nsh> bkfactory begin --transaction <32 位十六进制事务号> --confirm factory-init
BKFACTORY BEGIN PASS state=pending layout=559f52be
```

该记录绑定布局身份（`LAYOUT_SHA256_BYTES` 前 4 字节 + 完整 sha 存于 evidence），
事务号由本次部署生成并记录在工厂证据里。它是**唯一**允许设备初始化用户存储
的授权，且只对这一次首启有效。

## 6. 重启并交付给用户

```text
nsh> reset
```

判据：

- `BKFACTORY MOUNT PASS state=storage-ready format=1 identity=absent config=absent`
- `BKVOICE first boot state=ready ...`（身份已在设备侧生成并持久化）
- `BKDISPLAY CLAIM PAGE qr=1 version=5 dimension=37`
- `bkfactory status` 后续为 `identity-ready` / `unclaimed-ready`，用户认领后为
  `deployed`

随后**断开电脑**，把设备交给使用者，按
[普通用户首次使用指南](shaniu-firstuse-guide.md) 只用手机完成认领与配置。

## 7. 重复性检查

- 至少 3 轮“工厂整片部署 → 首启 → 扫码认领 → 首次对话”。
- 有条件时换第二块同型号板：两板身份必须不同，公开固件输入完全相同。
- 只有一块板时，明确记录“未完成跨板验收”。
- 原 TTS 连续播放、响应时间、OTA 与资源更新回归仍需保留。
