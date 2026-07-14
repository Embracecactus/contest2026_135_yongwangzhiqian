# 阶段 3 恢复提示词 — DCache 修复后重新烧录验证

> **用途：** `/clear` 后粘贴此内容以恢复上下文，继续阶段 2/3 工作。

---

## 背景

我在做 RV1126B HPMCU 的 openvela/NuttX 移植。阶段 1（构建修复）已完成，阶段 2（板端验证）进行中。

**平台参数：**
- RV1126B HPMCU（RISC-V）
- 串口：UART5M0, 1500000, 8N1
- RAM：0x48c02000, 大小 0x3a000（232KB）

**构建命令：**
```bash
cd /home/lijian/project/open-vela
export PATH=prebuilts/gcc/linux-x86_64/riscv-none-elf/bin:$PATH
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
```

**打包命令：**
```bash
riscv-none-elf-objcopy -O binary nuttx nuttx.bin
cp nuttx.bin $SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/rtt.bin
cd $SDK/rtos/bsp/rockchip/rv1126b-mcu
../tools/mkimage -f Image/amp.its -E -p 0xe00 Image/nuttx_amp.img
cp Image/nuttx_amp.img $SDK/output/firmware/amp.img
cd $SDK && ./build.sh updateimg
```

**SDK 路径：** `/home/lijian/project/rv1126b/rv1126bsportCam/sdk/atk_dlrv1126b_linux6.1_sdk/`

**烧录命令：**
```bash
cd $SDK/tools/linux/Linux_Upgrade_Tool/Linux_Upgrade_Tool/
./upgrade_tool di amp $SDK/output/firmware/amp.img
```

## 已完成的修复

### DCache 寄存器偏移和位域修复（rv1126b_start.c）

首次烧录结果：U-Boot 成功加载固件到 0x48c02000（SHA256 OK），但 UART5 无输出。

根因：DCache init 中所有寄存器偏移和位域值写错，导致：
1. uncache region 未正确配置 → 外设寄存器访问经过缓存
2. 可能卡在 DCache init 的 while 循环中

已修复的错误：

| 项目 | 错误值 | 正确值 |
|---|---|---|
| GRF_HPMCU_CACHE_ADDR_START | +0x0204 | +0x001C |
| GRF_HPMCU_CACHE_ADDR_END | +0x0208 | +0x0020 |
| DCACHE_CACHE_STATUS | +0x0004 | +0x0030 |
| DCACHE_STB_TIMEOUT_CTRL | +0x0008 | +0x000C |
| DCACHE_CACHE_ENTRY_THRESH | (7<<4) | (7<<8) |
| DCACHE_STB_TIMEOUT_EN_BIT | (1<<8) | (1<<7) |
| DCACHE_CACHE_BYPASS_BIT | (1<<16) | (1<<6) |

来源：SDK `hal/lib/CMSIS/Device/RV1126B/Include/rv1126b.h` 中的 GRF_SYS_REG 和 DCACHE_REG 结构体定义。

## 启动流程（已确认）

```
U-Boot 加载 amp.img FIT image 的 hpmcu 子镜像到 0x48c02000
  → fit_standalone_release("hpmcu", 0x48c02000)
  → CRU 断言 HPMCU 复位
  → ATF SMC: sip_smc_mcu_config(BUSMCU_0, CODE_START_ADDR, 0x48c02000)
  → CRU 释放 HPMCU 复位
  → NuttX __start 从 0x48c02000 开始执行
  → BSS 清零 → 数据段复制 → DCache init → 时钟配置 → UART5 init → nx_start()
```

## 下一步

1. 重新构建 NuttX（DCache 修复已应用）
2. 重新打包 amp.img 和 update.img
3. 烧录并观察 UART5 输出
4. 如果仍无输出，需进一步排查：时钟配置、pinmux、INTMUX 路由

## 协作规则

- 你（Opus）只做规划和审核，不要直接写代码
- 查代码必须先用 CodeGraph：`projectPath="/home/lijian/project/open-vela"`
- 具体修改和执行交给子代理（Sonnet/Haiku）
- 所有改动只在 `contest2026_135_yongwangzhiqian/` overlay 内
- 每完成一个子任务，更新 `docs/ai-worklog/2026-07-12-rv1126b-openvela-porting.md`
