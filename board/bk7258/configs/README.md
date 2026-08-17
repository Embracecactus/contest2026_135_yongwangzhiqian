# BK7258 compatibility seeds

`configs/` contains only the three retained role/bootstrap seeds listed
below.  It is not a board-by-application configuration matrix.  Fixed pins,
fitted devices and electrical limits remain under
[`../boards/`](../boards/README.md); applications are selected by Kconfig in
the build's final `.config`.

Every retained seed directory contains:

- `defconfig`: the NuttX configuration consumed by `build.sh`;
- `profile.conf`: board/role/boot/class/compatibility metadata consumed by
  `tools/bk7258/build_dual_image.sh` (from the repository root).

The flat directory layout is intentional: NuttX custom-board configuration
paths end at `configs/<profile>`.  The physical board is explicit in the
profile name and metadata.  T5-Board also carries its non-default
`CONFIG_BK7258_BOARD_T5_BOARD`; NuttX `savedefconfig` may omit the default
T5AI-Core choice.  Nesting by board would add a second, non-standard path
convention without removing the need for a profile name.

## Metadata contract

`profile.conf` schema 1 defines exactly these fields:

| Field | Meaning |
|---|---|
| `BK7258_PROFILE_SCHEMA` | Metadata format version; currently `1` |
| `BK7258_PROFILE_BOARD` | `t5ai_core`, `t5_board`, or `common` for BL2 |
| `BK7258_PROFILE_ROLE` | `cp`, `ap`, or standalone `bl2` |
| `BK7258_PROFILE_BOOT` | `raw` or `mcuboot` |
| `BK7258_PROFILE_CLASS` | `runnable`, bounded `validation`, `ci`, or BL2 `infrastructure` |
| `BK7258_PROFILE_COMPAT` | Exact CP/AP pairing group; both roles must match |
| `BK7258_PROFILE_SDK_BUNDLE` | Optional role-specific SDK bundle; omitted profiles use `v3.1.1.9` |

The dual-image builder rejects cross-board, cross-boot and incompatible
CP/AP combinations before compiling.  CI-only profiles additionally require
`BK7258_ALLOW_CI_PROFILE=YES`, so they cannot be mistaken for a board-runnable
image.

## Retained seed profiles

Only these three profile directories are retained as compatibility seeds:

| Profile | Role | Purpose |
|---|---|---|
| `bl2_mcuboot` | standalone BL2 | Common minimal MCUboot BL2 infrastructure seed |
| `t5ai_core_cp_base` | CP | T5AI-Core raw bring-up seed |
| `t5ai_core_ap_base` | AP | T5AI-Core raw bring-up seed |

They are inputs to canonical product resolution, not a 27-entry application
matrix.  Do not add per-feature or per-validation defconfigs; use a product
seed plus menuconfig/Kconfig (or a final .config) instead.

## Canonical products, seeds, and final .config

New builds are product-first, but the product never generates Kconfig
values.  Kconfig and the final `.config` are the only configuration
authority:

```text
Chip/Board Kconfig + App Kconfig
        ↓
build.sh <config> --cmake menuconfig
        ↓
final .config
        ↓
CMake build
        ↓
bk7258_framework.py verify-config
```

Product metadata (`bk7258_product_catalog_*.json`) keeps only identity,
expected board/role/boot, partition layout, SDK/ABI, trust/package and
artifact policy.  Roles with a retained seed (`legacy_profile`) start from
that seed's `defconfig`; seedless products must supply final `cp.config` /
`ap.config` / `bl2.config` files (``--config-root`` for the executor).

`tools/bk7258/bk7258_framework.py verify-config --product ... --role ... \
--config <final .config>` checks the locked board/role/boot facts and hashes
the final `.config` into build/package identity.  Validation suites in
`bk7258_validation_suite_catalog.json` are resource/behavior metadata only
and never inject Kconfig symbols.

## Usage

Resolve a canonical product without compiling or requiring signing keys:

```sh
python3 tools/bk7258/bk7258_framework.py build-plan \
  --product t5ai_core_bringup \
  --out /tmp/bk7258-t5ai-core-build-plan.json
```

Prepare the canonical isolated four-role contract:

```sh
python3 tools/bk7258/bk7258_isolated_executor.py prepare \
  --product t5ai_core_bringup \
  --build-root /tmp/bk7258-t5ai-core-build \
  --out /tmp/bk7258-t5ai-core-build/execution.json
```

The legacy `build_dual_image.sh` adapter remains a compatibility path only;
do not use old profile-pair commands as the canonical product interface.

MCUboot profiles require the external signing and BL1 manifest keys already
required by the secure-build pipeline.  Do not store those private keys in
the repository.

## Do not add application profiles

Do not add a `configs/<board>_<app>` directory for applications, services,
benchmarks or validation commands.  Select those through Kconfig/menuconfig
in the build-local final `.config`.  A new persistent seed is justified only
for a genuinely new boot or role bootstrap contract, not for a feature set.

## Seed 命名规则

- runnable 角色 seed：`<board_variant>_<role>_base`，例如
  `t5ai_core_cp_base` / `t5ai_core_ap_base`。
- standalone 基础设施 seed：`<role>_<boot_flavor>`，例如 `bl2_mcuboot`
  （`BK7258_PROFILE_BOARD=common`，跨板共用）。
- 禁止新增 feature / validation 级 defconfig；用 seed + menuconfig/Kconfig
  或最终 `.config` 表达应用差异。

## profile.conf 定位

`profile.conf` 是 contest 仓自有的 host 侧元数据，官方 NuttX/openvela 不
要求（官方 board 配置目录只有 `configs/<cfg>/defconfig`，`build.sh` /
CMake 也只消费 defconfig）。它只声明期望的
`BK7258_PROFILE_BOARD/ROLE/BOOT/CLASS/COMPAT` 事实，供 host 工具核对最终
`.config` 与组合校验；不生成、不覆盖任何 Kconfig 值。

## seed 不回写

`menuconfig` 修改构建目录中的最终 `.config`。禁止针对这三个冻结 seed 运行
会把精简结果写回源码 seed 的 `savedefconfig`；如需保存某次验收配置，应将
最终 `.config` 作为仓外构建证据保存。App 选择必须由最终 `.config` 表达，
不得生成 board×app defconfig 变体。
