# Current Progress

Last updated: 2026-08-09 GMT+8
Updated by: Qoder (takeover of codex session 019fb3ff)

## Active scope

The active objective is a recoverable BK7258 chain:

```text
legacy BootROM -> board-owned minimal BL1 -> signed Manifest
-> pinned NuttX MCUboot BL2 -> signed same-slot CP/AP pair -> NuttShell
```

Official BK7258 v3.1.1.9 has no buildable Secure Boot adaptation. BK7236
security material is used only as a same-architecture semantic/source
reference; its single-core addresses, OTP/eFuse ABI and TF-M mapping are not
treated as BK7258 facts. NuttX and SDK source trees remain unchanged.

## Current board baseline

- MCUboot version: `18.1.3`; protected security counter: `20`.
- BL1 profile: `BL1_MINIMAL=1`, fixed Primary -> Secondary BL2 ordering.
- BL1 responsibilities: clock/reset normalization, watchdog fail-closed,
  Manifest P-256/SHA-256 verification, BL2 vector/copy validation, checked
  SRAM policy publication and BL2 handoff.
- The final BL1 does not link N15/N17 lifecycle selectors, OTA Flash writer,
  N17 release keys or NuttX ECC. Historical validation profiles remain
  separate and are not part of the MCUboot image.
- BL2 remains the only component that validates and launches a signed CP/AP
  pair. It uses the pinned NuttX MCUboot sources and board-owned Flash/AP
  handoff adapters.
- Final BL1 ELF: `.text + .rodata = 9,878` bytes, `.data = 0`, `.bss = 0`.

Artifact SHA-256:

- `bl_crc.bin`: `b13e9946d0120a170836bef0bf97c2de953ae78db71016c8c6ae8ba9412a49ea`
- `all-app-factory.bin`: `bb80db82a1631112602902069d691a65a99710f74d7f2ac9d537cae796009cbe`
- `bl2_crc.bin`: `535571b677f0ced7d2c8a49b2495fbc0b2778657dfab50cb732c56a106204f17`

## Verification

- Full `JOBS=32` CP/AP MCUboot build passed using immutable SDK v3.1.1.9.
  The build now runs the profile-aware BL1 symbol verifier.
- Host mailbox/BL1-policy tests passed: `0/31` failures.
- Valid factory package reached
  `B1PRIMARY -> BL2RAM -> B2GOOK -> B2SELA -> B2APOK -> B2HANDOFF -> NSH`:
  `logs/bk7258-secureboot-minimal-primary/20260808-164835`.
- Corrupting byte `0x40` of only the Primary Manifest digest, then rebuilding
  its valid 32+2 CRC envelope, produced
  `rc=2 -> B1PRIMARY BAD -> B1SECONDARY -> B2HANDOFF -> NSH`:
  `logs/bk7258-secureboot-minimal-negative/20260808-165028`.
- The valid boot envelope was restored and passed:
  `logs/bk7258-secureboot-minimal-restored/20260808-165102`.
- Independent 150 ms COM7 RTS physical reset passed the Primary path with
  `cold_path=yes`:
  `logs/bk7258-secureboot-minimal-rts/20260808-165125`.
- The board is currently restored to the valid Primary image. No OTP/eFuse,
  secure lifecycle or debug-lock bit was written.

Canonical detail:
[Secure Boot remaining-gates verification](verification/2026-08-08-bk7258-secureboot-remaining-gates.md).

## Honest boundary

This proves a repository-owned, software-rooted Secure Boot chain on BK7258.
It does not prove that BK7258 BootROM consumes the candidate Manifest, and it
does not provide an immutable hardware root or persistent hardware-backed
anti-rollback. The board remains recoverable for unfinished driver work.

## AP peripheral wrapper checkpoint

- Reviewed CodeBuddy lower-half candidates were moved into the board-owned AP
  layer; NuttX and SDK sources remain unchanged.
- AP-SMP/AP-UP source selection and Kconfig now cover AUD, GPIO expander,
  I2C, I2S, LCD, microphone capture, RTC, SARADC, SDIO, SDMADC, SPI and timer.
  AUD and microphone capture are mutually exclusive owners of the AUD ADC.
- LCD framebuffer storage now comes from the established AP PSRAM heap rather
  than consuming about 300 KiB of AP SRAM `.bss`.
- Two AP-SMP compile/link profiles passed: all non-PWM wrappers plus AUD, and
  all non-PWM wrappers plus microphone capture. Both completed board CRC
  post-processing. This is compile evidence, not peripheral hardware proof.
- PWM is intentionally excluded: immutable v3.1.1.9 `libdriver.a` exports no
  `bk_pwm_*` API required by the candidate. A board-owned register wrapper or
  a source-verified SDK adaptation is still required.

Canonical detail:
[AP driver compile verification](verification/2026-08-08-bk7258-ap-drivers.md).

- The object-returning lower halves (GPIOE, I2S, SDIO, SPI, LCD) are now
  bound to their NuttX upper halves in `bk7258_peripherals_initialize()` so
  the devices are reachable from user space; bindings are best-effort and log
  instead of parking the AP. AP link now includes `libavdk_utils.a` for the
  SDK GPIO IPC checksum path, and `chip/Make.defs` adds the `arm_m` include
  directory for post-distclean dependency passes.
- AP-SMP `ap_smp_drivercheck` profile (AUD, GPIOE, I2S, LCD, SDIO, SPI)
  passed configure/compile/link/postbuild: `app1.bin=179888`,
  `app1_crc.bin=191148`.

Canonical detail:
[AP lower-half bindings compile gate](verification/2026-08-09-bk7258-ap-lowerhalf-bindings.md).

## Next step

1. Hardware-verify the bound peripherals one at a time (GPIO pinmux, SD card
   detect, I2S clocking, LCD panel timing, SPI chip select); do not enable
   all devices by default in shipped configs.
2. Implement PWM only after its v3.1.1.9 hardware/API boundary is source
   verified; do not add missing symbols to the immutable SDK bundle.
3. Resume N17 OTA policy on the recoverable Secure Boot baseline. Do not put
   historical N15/N17 writers back into minimal BL1.
4. Hardware Secure Boot provisioning is the final gate, after signed OTA and
   recovery matrices are stable and preferably on a second board. It requires
   separate authorization before any OTP/eFuse or lifecycle operation.

## Open constraints

- Official runtime SDK is fixed to v3.1.1.9; BK7259/v4 artifacts are excluded.
- Do not modify NuttX or SDK sources except temporary debugging that is fully
  restored.
- Private signing keys must never enter the repository, firmware logs or
  project memory.
- Existing runtime warnings `gpio: 0 is used` and
  `[ipc_svr] create_socket failed` are outside this boot-chain verification.
