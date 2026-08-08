# Verification: AP object-returning lower-half bindings compile gate

- Date and time zone: 2026-08-09, GMT+8
- Verifier: Qoder (takeover of codex session 019fb3ff)
- Commit or artifact: pending commit on `feat/bk7258-driver-integration`,
  parent `25fb43d`
- Environment: WSL2, `/home/lijian/project/open-vela/nuttx` configured with
  `tools/configure.sh -e
  ../.worktrees/bk7258-drivers/board/bk7258_t5ai/configs/ap_smp_drivercheck`,
  prebuilt `arm-none-eabi` toolchain from `prebuilts/build-tools`

## Scope

Compile/link gate for three uncommitted CodeBuddy changes carried over from
the codex session:

1. `chip/ap/bk7258_peripherals.c` binds the object-returning lower halves
   GPIOE, I2S, SDIO, SPI and LCD (`fb_register`) to their NuttX upper halves.
2. `scripts/Make.defs` adds `libavdk_utils.a` to the AP `EXTRA_LIBS` because
   the SDK GPIO driver reaches `bk_ipc_send` and the IPC frame checksum.
3. `chip/Make.defs` adds the `arm_m` include directory so a post-distclean
   dependency pass resolves the relocated Cortex-M headers.

## Commands or methods

```bash
cd /home/lijian/project/open-vela/nuttx
PATH=/home/lijian/project/open-vela/prebuilts/build-tools/linux-x86_64/bin:$PATH \
  ./tools/configure.sh -e \
  ../.worktrees/bk7258-drivers/board/bk7258_t5ai/configs/ap_smp_drivercheck
PATH=... make olddefconfig
PATH=... make -j32
```

The `ap_smp_drivercheck` defconfig enables AUD, GPIOE, I2S, LCD, SDIO and
SPI, so every new binding is compiled and linked by this profile.

## Results

- Configure, `olddefconfig`, compile, link and `postbuild.sh` all passed
  (exit 0).
- Partition check passed: `layout_id=bk7258-v3119-ab-7f14d67587a17bf9`,
  16 partitions.
- `app1.bin=179888` bytes, `app1_crc.bin=191148` bytes, SHA-256
  `77eff9f84c6ff124c74b3c8b758409811cffa24f9fd5b88a14e3b66d237efef1`.
- `bk7258_peripherals.o` present in the build tree; all six driver configs
  active in the final `.config`.

## Failures and investigation

None during this gate.

## Residual risks

- Compile/link evidence only. GPIO pinmux, card detect, I2S clocking, LCD
  panel timing and SPI chip-select behaviour remain hardware gates.
- The MIC variant of the audio block was not rebuilt: the gated change does
  not touch AUD/MIC drivers, and the earlier dual-profile gate already
  covered both.
- PWM remains excluded: `libdriver.a` (v3.1.1.9) exports no `bk_pwm_*`;
  see `chip/ap/PWM_BLOCKED_ROOT_CAUSE.md`.

## Evidence locations

- Build log tail: `postbuild.sh: role=ap app1.bin=179888 bytes
  app1_crc.bin=191148 bytes` (gate exit 0).
- Configuration:
  `board/bk7258_t5ai/configs/ap_smp_drivercheck/defconfig` (untracked
  validation config, intentionally not committed).
