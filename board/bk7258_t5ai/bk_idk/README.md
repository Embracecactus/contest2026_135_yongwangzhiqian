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
  armino_as_lib/
    versions/
      legacy/
        cp/{include,config,libs} # 迁移前 CP，不覆盖
        ap/{include,config,libs} # 迁移前 AP，不覆盖
      v3.1.1.9/
        cp/{include,config,libs}
        ap/{include,config,libs}
```

当前 bundle 文件数：

| 版本 / 角色 | include | config | `.a` | loose `.obj` |
|---|---:|---:|---:|---:|
| `legacy/cp` | 341 | 2 | 81 | 4 |
| `legacy/ap` | 603 | 2 | 101 | 0 |
| `v3.1.1.9/cp` | 341 | 2 | 81 | 0 |
| `v3.1.1.9/ap` | 603 | 2 | 101 | 0 |

CP 的 4 个 legacy loose object 不参与 NuttX 链接。

## 选择版本

普通构建不设置变量时使用 `v3.1.1.9`：

```bash
JOBS=8 board/bk7258_t5ai/scripts/build_dual_image.sh
```

下列legacy选择方式只作为历史恢复说明保留，当前规则禁止执行：

```bash
BK7258_SDK_BUNDLE_VERSION=legacy JOBS=8 \
  board/bk7258_t5ai/scripts/build_dual_image.sh
```

Classic Make 与 CMake 都只接受 `legacy` 或 `v3.1.1.9`，未知版本会立即报错，不会
静默链接到其他目录。构建产物中的 `build-profile.txt` 会记录所选版本、CP/AP 实际
目录、manifest 和 provenance 哈希。

## 校验

默认校验最新 CP，完整校验应显式检查两个角色：

```bash
board/bk7258_t5ai/scripts/setup_bk7258_sdk.sh \
  --check --version v3.1.1.9 --role cp
board/bk7258_t5ai/scripts/setup_bk7258_sdk.sh \
  --check --version v3.1.1.9 --role ap
```

legacy回退包保留有完整清单；下列命令当前禁止执行，仅供未来另行批准的兼容性任务参考：

```bash
for role in cp ap; do
  board/bk7258_t5ai/scripts/setup_bk7258_sdk.sh \
    --check --version legacy --role "${role}"
done
```

清单和来源记录位于：

```text
scripts/sdk-manifests/<version>/<role>.sha256
scripts/sdk-manifests/<version>/<role>.provenance
```

## 重新编译和导入

完整命令及官方 archive/source 路径见
[`docs/bk7258-t5ai/nuttx-port/sdk-static-library-import.md`](../../../docs/bk7258-t5ai/nuttx-port/sdk-static-library-import.md)。
脚本会重新编译 SDK UART 对象并加入 `CONFIG_BK_PRINTF_DISABLE`，避免 SDK 初始化接管
NuttX console。`legacy` 被设为不可替换；`--replace` 只允许显式替换非 legacy
版本，旧的 `--force` 已移除。

## 分发边界

若未来要把 bundle 自包含分发，必须先取得 Beken 的明确再分发授权。可参考
BK7236N `vendor_beken` 的独立 vendor 仓、许可证说明和普通 Git blob 模式；在取得
授权前，本仓只保留可复现的导入流程和 SHA-256 证据。
