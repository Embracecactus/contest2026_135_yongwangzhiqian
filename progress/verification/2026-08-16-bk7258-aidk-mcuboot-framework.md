# BK7258 AIDK AI Toy MCUboot framework verification

Date: 2026-08-16

## Conclusion

`CODE_PASS / HARDWARE_NOT_RUN`

The AIDK AI Toy product successfully used the existing BK7258 BL1/BL2,
MCUboot A/B, partition, signing, pairing and payload-container pipeline. This
phase proves the build/package framework only. It does not authorize Flash or
claim AIDK boot/runtime success.

## Source scope

- Base commit:
  `dee20b901ad852e5115fd76d9346bdf00d0c9e0e`
- Branch: `dev-ai-contest-2026`
- Product: `aidk_ai_toy_bringup`
- Image version: `18.6.81`
- The product materializes temporary CP/AP compatibility profiles without
  adding a persistent legacy config directory.
- The original AIDK factory FAL/raw image is reverse-engineering reference
  only and is not consumed by this product.

## Build and host evidence

- Focused AIDK/framework unit tests: 20 passed.
- Focused bkpack container tests: 4 passed; public `vela_*` standard aliases
  are disjoint from all `apps`, `normal` and `factory` Flash-plan members.
- Focused transport tests: 5 passed, including fail-closed rejection when the
  Windows loader omits either required success marker or reports a failure.
- `build_dual_image.sh` shell syntax: passed.
- Partition generator/check, partition verifier and SDK wrapper: passed as
  part of the signed dual-image build.
- BL1/BL2, CP/AP, MCUboot pair, factory-layout and bkpack generation: passed.
- `git diff --check`: passed.

The final payload container is:

```text
file: nuttx/bk7258-dual/firmware.bkpack
sha256: aaaf1d34fec14418dd068cc6ad304e1088b6d58532cb7b46794e0559970baa03
schema: bk7258.payload-bkpack/1
members: 30
plans: apps, normal, factory
```

The final verifier reported:

```text
status=pass
authenticated=false
hardware_verified=false
target_preflight_required=true
```

## Resolved product properties

- Both role configs select `BK7258_BOARD_AIDK_AI_TOY` and
  `BK7258_MCUBOOT_IMAGE`; T5AI-Core and T5-Board are not selected.
- CP UART0 is 115200, 8 data bits, no parity, one stop bit and no flow control.
- SWD and BL1/BL2 boot hold are disabled.
- The AP image links `g_bk7258_aidk_binding` with optional Audio/MIC/SDIO
  bindings absent. Audio, MIC, LCD, DVP, SDIO and TF are disabled.
- Archived CP/AP configs use stable `bk7258-product:` identifiers. The package
  does not contain a deleted materializer `/tmp` path.

## Artifact boundary

The OpenVela-facing role artifacts are byte-exact aliases:

```text
vela_cp.bin -> cp-raw.bin
vela_ap.bin -> ap-raw.bin
```

They are logical role images and are not directly Flashable on BK7258. Beken
internal images have separate purposes:

```text
app.bin / app1.bin             MCUboot-signed CP/AP logical images
app_crc.bin / app1_crc.bin     Beken 32+2 CRC-expanded streams
app_crc_flash.bin              Flash-padded CP payload
app1_crc_flash.bin             Flash-padded AP payload
```

The generated `WINDOWS_FLASH.txt` selects only the Flash-padded members and
explicitly rejects direct use of `vela_cp.bin` or `vela_ap.bin`.

## Unverified boundaries

- No COM9 write, reset, boot, UART capture or GPIO runtime test ran.
- The package is not an official OpenVela container format; `firmware.bkpack`
  is the BK7258 delivery extension around OpenVela role artifacts.
- No accepted AIDK initial-provisioning trust procedure exists. The package
  remains build-only until target identity/preflight is solved without
  weakening the normal trust gate.
- No AIDK peripheral driver is claimed by this phase.
