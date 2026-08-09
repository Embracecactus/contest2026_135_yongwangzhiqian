# Current Progress

Last updated: 2026-08-09 GMT+8
Updated by: Codex

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

- `bl_crc.bin`: `2e00debb90f720359bc78996eb79a68c1ae00aa8e2ede9626c64534ee62a51df`
- `all-app-factory.bin`: `b7103c3980e3557d4a544a4bb3b3ee9c3df29deaed164b5a293fab2257fda7f0`
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

## Drivercheck MCUboot board verification pass (2026-08-09)

The temporary probe baseline reached READY with 13 /dev nodes registered;
init evidence for AUD,
GPIOE, I2C, I2S, RTC, SARADC, SDIO, SDMADC, SPI, timer all zero;
runtime self-check read `/dev/gpio0` (level 1) and `/dev/rtc0`
(tm_year=70); ADC nodes open but raw reads need channel setup.

Root causes fixed and board-verified this pass:

1. GPIOE hang: NuttX `gplh_setpintype` issues WAKEUPCFG; the SDK wakeup
   APIs block forever on a gpio_ipc SYNC channel with no CP responder.
   Board wrapper now returns `-ENOTSUP` for WAKEUPCFG (permanent fix).
2. PSRAM dead: `cp_nsh_drivercheck_mcuboot` lacked
   `CONFIG_BK7258_PSRAM=y`, so CP never initialised the PSRAM
   controller; every access read 0 from both cores. Config fixed
   (vendor + board defconfigs); heap confirmed live (guard node 9,
   free node 0x9fffe, 624 KiB free).
3. LCD framebuf -ENOMEM: consequence of (2), now allocates.
4. `lcd_drv_init=-4096`: the PM vote alone was insufficient.  The missing
   prerequisite was the official SDK ordering
   `sys_drv_init -> ipc_init -> mb_ipc_init -> bk_ipc_init`, plus a retained
   `.ipc_chan_reg` table with real linker boundaries.  A board-owned minimal
   runtime now supplies only that chain on CP and AP; the full SDK
   `driver_init()` is not used.  Hardware then reported LCD driver/RGB init 0.
5. Generic LCD framebuffer `-ENOMEM`: it allocated a second 300 KiB SRAM
   shadow buffer.  The board driver now implements `fb_vtable_s` directly and
   registers its existing PSRAM scanout buffer as `/dev/fb0`.
6. GPIO/bus conflicts: the GPIOE integration no longer auto-registers and
   configures GPIO0..15.  Consumers must explicitly claim each pin; I2C,
   I2S, SPI, SDIO and LCD hardware tests use pin-compatible profiles.
7. TRNG: the standard NuttX `/dev/random` path is backed by the AP-owned
   v3.1.1.9 `bk_fill_rand()` implementation.  A temporary fail-closed probe
   read two independent 32-byte samples and AP reached READY; the probe was
   then removed and the clean image was rebuilt, reflashed and rechecked.
8. QSPI: the AP-owned v3.1.1.9 controller is exposed through NuttX
   `qspi_dev_s`.  The drivercheck MCUboot profile compiles, links and completes
   postbuild using only the immutable SDK bundle.  No arbitrary MTD is bound:
   the verified SDK command subset and 256-byte program transaction limit do
   not justify claiming general Flash compatibility.  Hardware transfer is
   pending a known external device and a pin-compatible profile because both
   QSPI controllers overlap active LCD/SDIO/SPI/I2S pins.
9. Touch: the CP-owned v3.1.1.9 controller is exposed as the standard NuttX
   `/dev/buttons` device for one selected channel.  The implementation avoids
   the SDK ISR's hidden `TIMER_ID1` ownership by polling on LPWORK.  A signed
   MCUboot board image reached NSH, registered the node and returned the real
   channel-3 bit in a four-byte read.  Physical capacitive transition evidence
   still requires a suitable electrode; the module's GPIO29 is USERKEY.
10. CAN, Ethernet, USB Host/Device, DVP, DMA2D, JPEG encode/decode,
   Scale/Rotate and YUV/H264 are source-audited blockers rather than fake
   implementations.  The immutable SDK bundle either omits the required data
   plane, owns it through another stack, or lacks the cache/error/buffer
   contract needed by the corresponding NuttX upper half.  No placeholder
   device, private character ABI or copied SDK source was added.
11. P2 review is complete: LIN, Segment LCD, IRDA and FFT/SBC remain blocked
   by missing immutable-bundle symbols, unresolved core ownership, absent
   NuttX/board consumer contracts, or a combination of those constraints.

All temporary shared-SRAM/device-list/allocation probes and temporary
`apctl` debug commands have been removed.  The final 32-job MCUboot build and
sparse flash passed (`logs/bk7258-auto-debug/20260809-122731`); a subsequent
read-only `apctl status` reported AP `READY(2)` with heartbeat 1106, CPU2
`SECONDARY_READY(7)` and AP IPI `READY(2)` with zero loss/failure.  RPTUN
remains `CONNECTING(3)` in this drivercheck profile.

Canonical detail:
[AP peripheral board evidence](verification/2026-08-09-bk7258-ap-peripheral-board-evidence.md).

## Next step

1. The isolated P0/P1/P2 driver queue is complete.  TRNG and CP touch are
   hardware-verified and QSPI is compile/link-verified; all non-implementable
   entries are recorded against an explicit immutable SDK/NuttX ABI boundary.
   Revisit them only after a v3.1.1.9 bundle is rebuilt with the relevant
   controller enabled and its public ownership/cache/buffer contract frozen.
2. Hardware-verify existing peripherals one at a time using pin-compatible
   profiles (SD card detect, I2S clocking, LCD pixels, SPI chip select); do
   not enable conflicting devices by default in shipped configs.
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
- Remaining GPIO/SPI warnings in the all-driver image are known physical-pin
  conflicts; transfer validation uses pin-compatible profiles.  RPTUN
  CONNECTING/`[ipc_svr]` behavior is tracked separately from this peripheral
  runtime checkpoint.
