# BK7258 build profiles

`configs/` contains build profiles, not physical-board descriptions.  One
physical board may have several profiles for normal applications, optional
services and bounded hardware validation.  Fixed pins, fitted devices and
electrical limits remain under [`../boards/`](../boards/README.md).

Every profile directory contains:

- `defconfig`: the NuttX configuration consumed by `build.sh`;
- `profile.conf`: board/role/boot/class/compatibility metadata consumed by
  `../scripts/build_dual_image.sh`.

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

## Retained profiles

### T5AI-Core

| Purpose | CP | AP | Boot/class |
|---|---|---|---|
| normal base | `t5ai_core_cp_base` | `t5ai_core_ap_base` | raw, runnable |
| signed base | `t5ai_core_cp_mcuboot` | `t5ai_core_ap_mcuboot` | MCUboot, runnable |
| PSRAM/SMP/BLE regression | `t5ai_core_cp_psram_validation` | `t5ai_core_ap_psram_validation` | raw, validation |
| Wi-Fi | `t5ai_core_cp_wifi` | `t5ai_core_ap_wifi` | raw, runnable |

### T5-Board

| Purpose | CP | AP | Boot/class |
|---|---|---|---|
| normal application | `t5_board_cp_app_mcuboot` | `t5_board_ap_app_mcuboot` | MCUboot, runnable |
| camera smoke validation | `t5_board_cp_app_mcuboot` | `t5_board_ap_camera_validation_mcuboot` | MCUboot, validation |
| camera/H.264 application | `t5_board_cp_app_mcuboot` | `t5_board_ap_camera_h264_mcuboot` | MCUboot, validation |
| PWM validation | `t5_board_cp_app_mcuboot` | `t5_board_ap_pwm_validation_mcuboot` | MCUboot, validation |
| TF 1-bit, UART0 download route retained | `t5_board_cp_app_mcuboot` | `t5_board_ap_tf_1bit_validation_mcuboot` | MCUboot, validation |
| TF 4-bit exclusive-route validation | `t5_board_cp_app_mcuboot` | `t5_board_ap_tf_4bit_validation_mcuboot` | MCUboot, validation |
| on-die temperature validation | `t5_board_cp_app_mcuboot` | `t5_board_ap_temperature_validation_mcuboot` | MCUboot, validation |
| Wi-Fi | `t5_board_cp_wifi_mcuboot` | `t5_board_ap_wifi_mcuboot` | MCUboot, runnable |
| compile-only driver coverage | `t5_board_cp_drivercheck` | `t5_board_ap_drivercheck` | raw, CI only |

`bl2_mcuboot` is the single common standalone NuttX BL2 infrastructure
profile and is not a CP/AP peer.  The signed dual-image pipeline builds the
board-owned minimal BL2 through `bootloader/bl2/Makefile`; it does not pair
this standalone defconfig with a physical-board application profile.

The TF profiles reflect physical switch ownership, not two different SDIO
drivers.  The one-bit profile works with S1-1/S1-2 ON and retains the CH342F
UART0 download route.  Before running the four-bit profile, set S1-1/S1-2
OFF; U3 must remain NC/DNP.  S1-3/S1-4 and P0/P1 SWD are independent of TF
width, so J-Link plus RTT may remain enabled with those two log-UART switches
OFF.  Both validation images use an already formatted FAT card and remove
only their uniquely created test file; they never format media.  P6 has no
verified insertion edge on the tested T5-Board, so both are fixed-media
profiles with `MMCSD_HAVE_CARDDETECT=n`: insert the card before reset and keep
it inserted for both validation cycles.

Do not switch S1-1/S1-2 ON while the four-bit image is actively using SDIO.
For a later COM3 download, first reset into the existing BL2 hold, then switch
S1-1/S1-2 ON and download.  After the loader returns to BL2 hold, switch both
OFF again before releasing the hold through P0/P1 SWD.

The default v3.1.1.9 AP SDK bundle was compiled without its private
`CONFIG_SDCARD_BUSWIDTH_4LINE` option and remains the one-bit implementation.
The four-bit profile binds `v3.1.1.9-sdio4`; its data helper is fixed at four
lines, so the lower half reports `SDIO_CAPS_4BIT_ONLY` and NuttX sends ACMD6
before the first data transfer (SCR).  The build rejects either bundle when
paired with the opposite profile instead of silently running at the wrong
width.

Stage reports and evidence records may still name retired `cp_nsh_*` or
`ap_smp_*` snapshots because those names identify the exact historical image
that produced the evidence.  They are not current build instructions; use the
table above for new builds.

## Usage

Check a pair without compiling or requiring signing keys:

```sh
BK7258_PROFILE_CHECK_ONLY=YES \
CP_CONFIG_NAME=t5ai_core_cp_base \
AP_CONFIG_NAME=t5ai_core_ap_base \
./board/bk7258/scripts/build_dual_image.sh
```

Build the raw T5AI-Core base pair:

```sh
CP_CONFIG_NAME=t5ai_core_cp_base \
AP_CONFIG_NAME=t5ai_core_ap_base \
./board/bk7258/scripts/build_dual_image.sh
```

The physical build path serializes access to the shared openvela `nuttx/` and
`apps/` trees.  A second dual-image build waits on the workspace lock instead
of replacing generated configuration or artifacts underneath the first.

MCUboot profiles require the external signing and BL1 manifest keys already
required by the secure-build pipeline.  Do not store those private keys in
the repository.

## Adding a profile

Add a profile only when it represents a reusable application/service set or a
bounded validation target.  Do not preserve each bring-up stage as another
defconfig.  Prefer extending an existing validation profile when the new gate
is cumulative, and use Kconfig/runtime control for ordinary peripheral
parameters such as UART baud, I2C frequency and SPI mode.
