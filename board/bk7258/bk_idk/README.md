# BK7258 T5-AI SDK bundles

`armino_as_lib/` 保存从已授权 Beken SDK 本地编译、导出的 CP/AP 头文件和静态库。
这些文件含受限预编译内容，已由 Git 忽略，不在本仓库分发；仓库只跟踪版本选择器、
导入脚本、校验清单和来源记录。

## 版本布局

项目只使用官方技术支持提供并已完成板测的`v3.1.1.9`。迁移前的`legacy`仍原样保留，
但当前不参与分析、构建或验证；只有当前功能完整完成并在v3.1.1.9实板闭环后，才能由owner另开
任务决定是否验证旧版本：

```text
bk_idk/
  sdk-bundles.mk
  sdk-profiles/
    v3.1.1.9/ap-peripherals-r2.config
    v3.1.1.9/ap-sdio4.config
  armino_as_lib/
    versions/
      legacy/
        cp/{include,config,libs} # 迁移前 CP，不覆盖
        ap/{include,config,libs} # 迁移前 AP，不覆盖
      v3.1.1.9/
        cp/{include,config,libs}
        ap/{include,config,libs}
      v3.1.1.9-sdio4/
        ap/{include,config,libs} # four-line data setup; AP only
```

当前 bundle 文件数：

| 版本 / 角色 | include | config | `.a` | loose `.obj` |
|---|---:|---:|---:|---:|
| `legacy/cp` | 341 | 2 | 81 | 4 |
| `legacy/ap` | 603 | 2 | 101 | 0 |
| `v3.1.1.9/cp` | 341 | 2 | 81 | 0 |
| `v3.1.1.9/ap` | 603 | 2 | 102 | 0 |
| `v3.1.1.9-sdio4/ap` | 603 | 2 | 102 | 0 |

CP 的 4 个 legacy loose object 不参与 NuttX 链接。

## 选择版本

普通构建不设置变量时使用 `v3.1.1.9`：

```bash
JOBS=8 tools/bk7258/build_dual_image.sh
```

下列legacy选择方式只作为历史恢复说明保留，当前规则禁止执行：

```bash
BK7258_SDK_BUNDLE_VERSION=legacy JOBS=8 \
  tools/bk7258/build_dual_image.sh
```

Classic Make 与 CMake 接受 `legacy`、`v3.1.1.9` 和 AP-only
`v3.1.1.9-sdio4`，未知版本会立即报错，不会静默链接到其他目录。普通 CP/AP
profile 都使用 `v3.1.1.9`；T5-Board 四线 TF profile 显式绑定 AP variant，CP 仍用
原版。构建产物中的 `build-profile.txt` 会分别记录 CP/AP 版本、实际目录、manifest
和 provenance 哈希。

## 校验

默认校验最新 CP，完整校验应显式检查两个角色：

```bash
tools/bk7258/setup_bk7258_sdk.sh \
  --check --version v3.1.1.9 --role cp
tools/bk7258/setup_bk7258_sdk.sh \
  --check --version v3.1.1.9 --role ap
tools/bk7258/setup_bk7258_sdk.sh \
  --check --version v3.1.1.9-sdio4 --role ap
```

The check is fail-closed for both files: the manifest must match every local
bundle byte, and provenance must uniquely bind the selected version and role
to the manifest and final `libdriver.a`.  The four-bit variant additionally
binds the exact ordered pair of tracked SDK configuration overlays.  Missing,
duplicated or mismatched identity fields stop the build.

legacy回退包保留有完整清单；下列命令当前禁止执行，仅供未来另行批准的兼容性任务参考：

```bash
for role in cp ap; do
  tools/bk7258/setup_bk7258_sdk.sh \
    --check --version legacy --role "${role}"
done
```

清单和来源记录位于：

```text
board/bk7258/bk_idk/manifests/<version>/<role>.sha256
board/bk7258/bk_idk/manifests/<version>/<role>.provenance
```

## 重新编译和导入

完整命令及官方 archive/source 路径见
[`docs/bk7258-t5ai/nuttx-port/sdk-static-library-import.md`](../../../docs/bk7258-t5ai/nuttx-port/sdk-static-library-import.md)。
脚本会重新编译 SDK UART 对象并加入 `CONFIG_BK_PRINTF_DISABLE`，避免 SDK 初始化接管
NuttX console。`legacy` 被设为不可替换；`--replace` 只允许显式替换非 legacy
版本，旧的 `--force` 已移除。

AP 使用仓库跟踪的 `ap-peripherals-r2` 配置层构建。四线 variant 在其上再叠加
`ap-sdio4.config`，只启用 SDK 数据 helper 所需的
`CONFIG_SDCARD_BUSWIDTH_4LINE`；它保持 `CONFIG_SDIO_4LINES_EN=n`，初始一线到
ACMD6 后四线的时序仍由 NuttX wrapper 控制。这些配置层只作用于复制到临时目录
的 SDK `projects/app`，不会修改官方 SDK 源码；它导出 PWM、CAN、DVP、Ethernet、
YUV、JPEG encoder 和 H.264 实现，同时保留已有 TRNG、QSPI、USB Host、DMA2D、
JPEG decoder、scale/rotate。静态库中存在能力不代表 NuttX 驱动已经完成：当前本仓
只正式接入 PWM，并为 DVP 提供尚待板级 sensor binding 的 imgdata lower half。

四线 AP bundle 的可复现导入命令为：

```bash
tools/bk7258/import_bk7258_sdk_role.sh \
  --sdk-dir /path/to/authorized/bk_avdk_smp \
  --role ap --bundle-version v3.1.1.9-sdio4 \
  --profile ap-peripherals-r2-sdio4 --build --replace
```

The final import holds the same lock as the dual-image build and replaces the
ignored bundle, tracked manifest and tracked provenance as one recoverable
transaction.  A failed command, post-install verification or catchable signal
restores the previous three-part set; a fresh checkout with only the two
tracked metadata files is also supported, but replacing either existing
metadata file still requires explicit `--replace`.

## 分发边界

若未来要把 bundle 自包含分发，必须先取得 Beken 的明确再分发授权。可参考
BK7236N `vendor_beken` 的独立 vendor 仓、许可证说明和普通 Git blob 模式；在取得
授权前，本仓只保留可复现的导入流程和 SHA-256 证据。
