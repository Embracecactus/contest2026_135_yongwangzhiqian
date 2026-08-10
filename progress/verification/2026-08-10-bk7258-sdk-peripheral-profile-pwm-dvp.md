# Verification: SDK peripheral profile, GC2145 capture and PWM board gate

- Date and time zone: 2026-08-10, GMT+8
- Verifier: Codex
- Branch: `feat/bk7258-sdk-peripheral-r2`
- Scope: board-owned wrappers and import tooling only; official NuttX and SDK
  sources unchanged

## Implemented scope

- Added the tracked `ap-peripherals-r2` build profile for the official BK7258
  SDK v3.1.1.9.  The import script copies `projects/app` to a temporary
  directory, applies the profile there, uses a temporary build root and
  imports the resulting headers/libraries with provenance and file hashes.
- The AP bundle now exports PWM, CAN, DVP, Ethernet, YUV, JPEG encoder and
  H.264 implementations.  Exported code is not treated as an adapted NuttX
  driver until a board wrapper selects and registers it.
- Completed the AP PWM lower half on NuttX `/dev/pwmN`, including SDK-to-errno
  mapping, 16.16 duty conversion, live period/duty update, per-channel
  ownership and failure propagation.
- Completed the generic DVP `imgdata_s` link closure, including the six
  v3.1.1.9 sensor-detect archive members and the SDK-required linker section.
- Added the T5-Board V1.0.2 P10 camera binding for NuttX V4L2 `/dev/video0`.
  It uses the board schematic pin map and PSRAM-backed SDK/V4L2 buffers.  The
  camera binding is mutually exclusive with the RGB LCD and GT1151 touch
  routes because the physical pins overlap.

## Build and board evidence

The final `cp_nsh_drivercheck` configuration was rebuilt with 32 jobs:

- `app.bin=217120` bytes
- `app_crc.bin=230690` bytes
- CRC image SHA-256:
  `d42b12441714b99a70cd9b6bdebc300a5f48cb7f29d8890b1a760774de2a766f`

The final `ap_smp_drivercheck` configuration, with generic DVP and PWM
enabled, was rebuilt with 32 jobs:

- `app1.bin=236220` bytes
- `app1_crc.bin=250988` bytes
- CRC image SHA-256:
  `5e7e2bd08364247371ba7b1666974640b298ac2c19c6711515ff037fd2bfafb7`
- final ELF contains `bk7258_pwm_initialize`, `bk_pwm_driver_init`, all six
  sensor detect functions and the sensor table range
  `0x02188854..0x0218886c` (six four-byte records)

A temporary camera-only AP configuration disabled the conflicting LCD/touch
routes and enabled `CONFIG_BK7258_T5_BOARD_CAMERA`.  Its full link passed:

- `app1.bin=257960` bytes
- `app1_crc.bin=274108` bytes
- CRC image SHA-256:
  `e955de3fefc27c16fb7be6fe840ab95d3e93ecc7fd0a45ef935220bcb41e74a7`
- final ELF contains `bk7258_t5_board_camera_initialize`,
  `bk7258_dvp_initialize`, `bk_dvp_open`, `capture_register`,
  `platform_is_in_interrupt_context` and the sensor table

The complete BL1 -> pinned NuttX MCUboot BL2 -> signed CP/AP pair build also
passed with version `18.1.4` and security counter `21`.  Partition generation,
32+2 CRC encoding and factory-layout validation passed.  Relevant artifact
hashes were:

- `bl_crc.bin`:
  `ef6690dce7976b7c0fd44d5ab0974c0cf2be895f23b61f926bfe5cd7a15c9ce7`
- `bl2_crc.bin`:
  `1ce5a10153e51452eb7871f7e57c009522d80c2a1850f2ab5b69ae5c2a1af79e`
- `all-app-factory.bin`:
  `4fdd2c5a7deab052d2c2a5e1130e5128a5d2ea463bd86e3f5e399634b7b5a709`

The dedicated T5-Board camera profile was subsequently built, sparsely
flashed and exercised through the NuttX V4L2 API.  The final source build used
MCUboot version `18.1.45` and security counter `62`.  Runtime reported:

```text
BKCAM PASS bytes=11507 format=MJPEG size=640x480
```

The bounded capture opened `/dev/video0`, captured one GC2145 640x480 MJPEG
frame into PSRAM, and checked both JPEG SOI and EOI markers before reporting
success.  BL1, BL2, CP/AP handoff, AP SMP and RPMsg syslog remained healthy.

The dedicated T5-Board PWM profile was built and sparsely flashed with
MCUboot version `18.1.49` and security counter `66`.  The attached RGB LCD
backlight is physically wired to P9/PWM3; P25 belongs to the separate SPI LCD
mapping and is RGB data G6 on the attached panel.  The final run reported:

```text
BKPWM duty=100% channel=3 gpio=9
BKPWM duty=0% channel=3 gpio=9
BKPWM duty=10% channel=3 gpio=9
BKPWM duty=50% channel=3 gpio=9
BKPWM duty=90% channel=3 gpio=9
BKPWM PASS channel=3 gpio=9 frequency=1000 duties=100/0/10/50/90%
```

The board owner visually confirmed the corresponding backlight changes.  An
earlier initialization sequence entered the v1px driver's special zero-duty
GPIO mode before the first real waveform and produced no visible change even
though SDK calls returned success.  Deferring `bk_pwm_init()` until the first
real NuttX characteristics are available matches the official SDK sequence
and resolved the hardware failure.

`git diff --check` and import-script `bash -n` both pass.

## Honest boundary

- PWM is board-verified through visible LCD backlight levels and the complete
  NuttX/SDK control path.  Absolute frequency, duty accuracy and edge quality
  have not been measured with an oscilloscope or logic analyzer.
- The T5-Board camera binding has one valid JPEG capture.  Long-duration
  streaming, sustained frame rate and concurrent display are not claimed.
- The 640 KiB AP PSRAM heap can hold the two 100 KiB SDK MJPEG frames, the
  20 KiB encoder scratch area and one 640x480 V4L2 MMAP buffer.  Multiple
  full-sized MMAP buffers may return `-ENOMEM`; USERPTR or a later PSRAM
  sizing decision is required for a deeper queue.
- CAN and Ethernet are exported for the next bounded driver stages, not
  registered as completed devices in this checkpoint.

## Next gate

The next bounded driver stage may consume the already-exported CAN lower half
without another SDK rebuild.  Source and link validation can proceed now;
physical CAN loopback remains pending until a transceiver is connected.
