# BK7258 build profiles

`configs/` contains build profiles, not physical-board descriptions.  One
physical board may have several profiles for normal applications, optional
services and bounded hardware validation.  Fixed pins, fitted devices and
electrical limits remain under [`../boards/`](../boards/README.md).

Every profile directory contains:

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
fragment or validation suite instead.

## Canonical products, fragments, and suites

New builds are product-first.  Resolve them with
`tools/bk7258/bk7258_framework.py` and execute the isolated contract
with `tools/bk7258/bk7258_isolated_executor.py`.

| Product | Board/boot | Base fragments | Role fragments | Retained seed mapping |
|---|---|---|---|---|
| `t5ai_core_bringup` | `t5ai_core` / raw | `common_base`, `board_t5ai_core`, `boot_raw` | `role_cp`, `role_ap`, `role_bl2` | CP/AP base and `bl2_mcuboot` |
| `t5_board_bringup` | `t5_board` / MCUboot AB | `common_base`, `board_t5_board`, `boot_mcuboot_ab` | `role_cp`, `role_ap`, `role_bl2` | `bl2_mcuboot` only |
| `aidk_ai_toy_bringup` | `aidk_ai_toy` / MCUboot AB | `common_base`, `board_aidk_ai_toy`, `boot_mcuboot_ab` | `role_cp`, `role_ap`, `role_bl2` | none; fully composed |

Canonical validation suites are resolved as product fragments rather than
profile directories:

- `t5ai_core_bringup`: `psram` (`validation_psram`);
- `t5_board_bringup`: `audio_dac`, `jpeg_m2m`, `saradc_key`, `temperature`,
  `camera`, `camera_h264`, `pwm`, `tf_1bit`, `tf_4bit`, `driver_coverage`, and
  `wifi` (each uses the matching `validation_*` fragment).

The authoritative product and fragment documents are the
`bk7258_product_catalog_*.json`, `bk7258_fragment_catalog_*.json`, and
`bk7258_validation_suite_catalog.json` files beside the framework.  The
validation suite catalog is the source for feature symbols and resource
requirements; it does not claim hardware PASS by itself.

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

## Adding a profile

Add a profile only when it represents a reusable application/service set or a
bounded validation target.  Do not preserve each bring-up stage as another
defconfig.  Prefer extending an existing validation profile when the new gate
is cumulative, and use Kconfig/runtime control for ordinary peripheral
parameters such as UART baud, I2C frequency and SPI mode.
