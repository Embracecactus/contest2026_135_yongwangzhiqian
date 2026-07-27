# BK7258 AP/CP SDK 静态库编译与导入

本文只描述 NuttX wrapper 使用的 AP/CP SDK 静态库流程。

## 1. 目录和链接关系

两套固件必须使用各自的 SDK 产物：

```text
CPU0/CP NuttX -> armino_as_lib/cp/{include,config,libs}
CPU1/AP NuttX -> armino_as_lib/ap/{include,config,libs}
```

`bk_idk/armino_as_lib/` 是本地受限 SDK 目录，已被 Git 忽略，禁止提交其中的头文件和二进制库。

## 2. 一条命令构建并导入

在 openvela 工作区执行：

```bash
cd /home/lijian/project/open-vela

ARMINO_SDK_DIR=/home/lijian/project/armino/bk_avdk_smp \
JOBS=8 \
./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/import_bk7258_sdk_role.sh \
  --role ap --build --force
```

CP 对应命令：

```bash
ARMINO_SDK_DIR=/home/lijian/project/armino/bk_avdk_smp \
JOBS=8 \
./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/import_bk7258_sdk_role.sh \
  --role cp --build --force
```

脚本自动使用 `arm-none-eabi-gcc` 所在目录覆盖 SDK 默认的 `/opt/...` 工具链路径。如需显式指定：

```bash
--toolchain-dir /usr/bin
```

## 3. 只更新 UART 对象

SDK 已经构建过、`compile_commands.json` 仍存在时，不必重新编译整个 SDK：

```bash
./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/import_bk7258_sdk_role.sh \
  --role ap --force
```

脚本执行以下操作：

1. 从对应角色的 `compile_commands.json` 提取 `uart_driver.c` 原始编译命令；
2. 添加 `-DCONFIG_BK_PRINTF_DISABLE`；
3. 单独重编 `uart_driver.c.obj`；
4. 用 `arm-none-eabi-ar r` 替换导出 `libdriver.a` 中的同名成员；
5. 导入该角色的 `include/`、`config/` 和 `libs/`；
6. 确认新对象不再引用 `bk_printf_init`。

对应 SDK 构建目录：

```text
CP: <SDK>/build/bk7258/app/bk7258/
AP: <SDK>/build/bk7258/app/bk7258_ap/
```

## 4. NuttX 侧链接规则

`CONFIG_BK7258_AP_CORE` 决定使用哪套 SDK：

- 未设置：使用 `armino_as_lib/cp`；
- 设置为 `y`：使用 `armino_as_lib/ap`。

AP 只链接驱动及其必要 HAL/PM/公共库。SDK 的 CMSIS startup、FreeRTOS、`libbk_rtos.a` 和 `libos_source.a` 不参与链接；启动、调度、堆和同步原语继续由 NuttX 管理。

## 5. 构建双镜像

导入两套 SDK 后执行：

```bash
cd /home/lijian/project/open-vela
JOBS=8 \
./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/build_dual_image.sh
```

输出目录：

```text
nuttx/bk7258-dual/
```

其中包括 CP `app.bin`、AP `app1.bin`、CRC 镜像、ELF 和双镜像清单。正常分区升级继续按清单中的 offset/length 写入，以保留 LittleFS。

## 6. 2026-07-27 同步后构建证据

在 clean PR 分支迁回主检出目录后执行完整双镜像构建，exit 0：

```text
/home/lijian/project/open-vela/contest2026_135_yongwangzhiqian/
  board/bk7258_t5ai/scripts/build_dual_image.sh
```

日志：`/tmp/bk7258-dual-build-sync-2026-07-27.log`。构建完成 CP → AP → CP restore，并通过
root CP image 与 dual manifest CP image 的 fail-closed 一致性检查。

角色隔离结果：

- `nuttx-cp.map` 引用 `armino_as_lib/cp`，不引用 `armino_as_lib/ap`；
- `nuttx-ap.map` 引用 `armino_as_lib/ap`，不引用 `armino_as_lib/cp`；
- CP/AP 均从各自 role 的 `libdriver.a` 取对象，没有跨角色静态库路径。

最终 normal split-update segments：

```text
bl_crc.bin@0x0-0x11000
app_crc.bin@0x11000-0x2c9bc
app1_crc.bin@0x220000-0x10b16
```

本次仅完成构建和静态角色隔离验证，未烧录、未板测，状态为 `build-verified`。
