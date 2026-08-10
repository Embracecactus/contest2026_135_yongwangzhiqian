# BK7258 AP/CP SDK 静态库编译、导入与回退

本文描述 team overlay 使用的 CP/AP SDK 静态库流程。它不会修改 NuttX 官方源码。

## 1. 当前版本策略

- 唯一active版本：`v3.1.1.9`，来自官方技术支持提供的最新压缩包；
- preserved版本：`legacy`，保留迁移前CP/AP bundle，不覆盖、不删除，但当前不参与分析、
  构建或验证；只有当前功能完整完成并在v3.1.1.9实板闭环后，才能由owner另开任务验证；
- GitHub 历史参考：`/home/lijian/project/armino/vendor_beken`，继续保留，不作为当前
  BK7258 链接输入；
- 旧 SDK 源码：`/home/lijian/project/armino/bk_avdk_smp`，继续保留；
- 最新 SDK 源码：`/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9`。

官方压缩包：

```text
Windows:
C:\Users\lijian\Downloads\BK7258_SMP\bk_avdk_smp-release-v3.1.1.9.tar.gz

WSL:
/mnt/c/Users/lijian/Downloads/BK7258_SMP/bk_avdk_smp-release-v3.1.1.9.tar.gz

size:
207144722 bytes

SHA-256:
39ae282d6d20f77734b7eed3ceb1c679427180d697b12ab3f61fcc39959efcbd
```

该发行包根目录的 `README.md` 和 `README_CN.md` 均为空文件，因此版本身份以官方
文件名、压缩包哈希、导入 provenance 和导出内容 manifest 共同固定。

## 2. 目录与链接关系

两套固件必须使用同版本、各自角色的产物：

```text
CPU0/CP NuttX -> armino_as_lib/versions/v3.1.1.9/cp/{include,config,libs}
CPU1/AP NuttX -> armino_as_lib/versions/v3.1.1.9/ap/{include,config,libs}
```

legacy历史映射仍保留如下，但当前禁止选择：

```text
CPU0/CP NuttX -> armino_as_lib/versions/legacy/cp/{include,config,libs}
CPU1/AP NuttX -> armino_as_lib/versions/legacy/ap/{include,config,libs}
```

`BK7258_SDK_BUNDLE_VERSION` 由 `bk_idk/sdk-bundles.mk`、Classic Make、CMake 和
双镜像脚本共同校验。默认值是 `v3.1.1.9`；未知值 fail closed。

`bk_idk/armino_as_lib/` 是本地受限 SDK 目录，已被 Git 忽略。禁止提交其中的头文件
和二进制库。

## 3. 从官方 SDK 编译并导入

在 openvela 工作区执行：

```bash
cd /home/lijian/project/open-vela

SDK_ARCHIVE=/mnt/c/Users/lijian/Downloads/BK7258_SMP/bk_avdk_smp-release-v3.1.1.9.tar.gz
SDK_TREE=/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9
IMPORT=./contest2026_135_yongwangzhiqian/board/bk7258/scripts/import_bk7258_sdk_role.sh

ARMINO_SDK_DIR="${SDK_TREE}" JOBS=8 "${IMPORT}" \
  --role cp --bundle-version v3.1.1.9 \
  --source-archive "${SDK_ARCHIVE}" --build --replace

ARMINO_SDK_DIR="${SDK_TREE}" JOBS=8 "${IMPORT}" \
  --role ap --bundle-version v3.1.1.9 \
  --source-archive "${SDK_ARCHIVE}" \
  --profile ap-peripherals-r2 --build --replace
```

脚本默认也指向 `v3.1.1.9` 和上述最新 SDK source tree。这里仍显式写出版本和路径，
便于审计、避免以后升级时误替换。

工具链默认从 `PATH` 查找，当前已验证版本为：

```text
arm-none-eabi-gcc (15:10.3-2021.07-4) 10.3.1 20210621 (release)
```

如需指定：

```bash
--toolchain-dir /usr/bin
```

`base` profile 的 SDK 已构建且 `compile_commands.json` 仍存在时，可省略
`--build`。非 base profile 必须带 `--build`，以确保 profile 与导出库严格对应。
导入脚本会：

1. 从对应角色的 `compile_commands.json` 提取 `uart_driver.c` 编译命令；
2. 添加 `-DCONFIG_BK_PRINTF_DISABLE`；
3. 单独重编 `uart_driver.c.obj`；
4. 替换导出 `libdriver.a` 中的同名成员；
5. 验证新对象不再引用 `bk_printf_init`；
6. 原子导入 `include/`、`config/` 和 `libs/`；
7. 生成全文件 SHA-256 manifest 和来源 provenance。

base profile 对应 SDK 构建目录：

```text
CP: <SDK>/build/bk7258/app/bk7258/
AP: <SDK>/build/bk7258/app/bk7258_ap/
```

`ap-peripherals-r2` 会复制 `projects/app` 到 `mktemp` 工作区、合并仓库跟踪的
`board/bk7258/bk_idk/sdk-profiles/v3.1.1.9/ap-peripherals-r2.config`，并把
`PROJECT_DIR`/`BUILD_DIR` 都指向该临时工作区。退出后临时目录自动删除，官方 SDK
源码及其原构建目录不发生修改。该 profile 导出 PWM、CAN、DVP、Ethernet、YUV、
JPEG encoder 与 H.264；NuttX 侧仍按 Kconfig 选择性链接、注册和初始化各 lower
half，未实现的 lower half 不会被伪装成已适配。

`legacy` bundle 永不可由导入脚本替换。非 legacy 版本也只有显式给出 `--replace`
才可原子替换；旧 `--force` 参数会直接报错。

## 4. 校验已安装 bundle

```bash
CHECK=./contest2026_135_yongwangzhiqian/board/bk7258/scripts/setup_bk7258_sdk.sh

for role in cp ap; do
  "${CHECK}" --check --version v3.1.1.9 --role "${role}"
done

for role in cp ap; do
  "${CHECK}" --check --version legacy --role "${role}"
done
```

跟踪文件：

```text
board/bk7258/scripts/sdk-manifests/<version>/<role>.sha256
board/bk7258/scripts/sdk-manifests/<version>/<role>.provenance
```

manifest 固定 bundle 内所有头文件、配置和库；provenance 固定官方 archive 哈希、
SDK 路径、工具链、UART patch 对象以及 patch 前后 `libdriver.a` 哈希。

## 5. NuttX 侧链接规则

`CONFIG_BK7258_AP_CORE` 决定角色：

- 未设置：使用所选版本的 CP bundle；
- 设置为 `y`：使用所选版本的 AP bundle。

AP 只链接驱动及其必要 HAL/PM/公共库。SDK CMSIS startup、FreeRTOS、
`libbk_rtos.a` 和 `libos_source.a` 不参与链接；启动、调度、堆和同步原语仍由
NuttX 管理。版本切换只发生在 team overlay 的 include/archive 路径层。

## 6. 构建双镜像与回退

默认构建最新 SDK：

```bash
cd /home/lijian/project/open-vela
JOBS=8 \
./contest2026_135_yongwangzhiqian/board/bk7258/scripts/build_dual_image.sh
```

临时回退 legacy：

```bash
BK7258_SDK_BUNDLE_VERSION=legacy JOBS=8 \
./contest2026_135_yongwangzhiqian/board/bk7258/scripts/build_dual_image.sh
```

输出位于 `nuttx/bk7258-dual/`。`build-profile.txt` 记录所用 bundle、CP/AP 真实路径、
manifest 和 provenance 哈希，map 文件可用于确认没有跨角色、跨版本链接。

正常分区升级继续按 `bk7258-dual-image.json` 的 offset/length 写入，以保留
LittleFS。`all-app-factory.bin` 是整片工厂镜像，会覆盖对应填充区域。

## 7. 迁移验证结论

`v3.1.1.9` 已完成：

- 官方 SDK CP/AP 全量编译；
- CP/AP bundle 全文件校验；
- legacy/latest ABI、头文件和 archive 比较；
- legacy/latest 双镜像完整构建及角色路径隔离；
- 最新工厂镜像烧录、warm 启动和物理 RESET 冷启动 3/3；
- `git -C /home/lijian/project/open-vela/nuttx diff --exit-code -- .`，NuttX 官方源码
  零改动。

详细哈希、ABI 差异及板测日志见
[`sdk-v3.1.1.9-migration-report.md`](sdk-v3.1.1.9-migration-report.md)。
