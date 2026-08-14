# BK7258 microphone topology and lifecycle verification

Date: 2026-08-14

## Scope

Close the shared BK7258 NuttX microphone lower-half for two physical-board
topologies and validate T5-Board through the public audio API:

- T5AI-Core: MICP1/MICN1, one logical channel.
- T5-Board: MICP1/MICN1 and MICP2/MICN2, two logical channels.

Board headers own fitted-channel facts.  Kconfig owns product defaults for
sample rate, gains and buffering.  Applications retain runtime format control
through `/dev/audio/pcm0c`.

## Root cause and correction

The immutable v3.1.1.9 AP `bk_aud_driver_init()` performs two PM operations:
it votes SDK power submodule `122` on, then enables device clock `30`.  The
clock call was already wrapped to the CP RPMsg PM service, but the power vote
was not wrapped for a MIC-only build.  It therefore used the obsolete native
CPU1-to-CPU0 SDK mailbox after that mailbox had become RPTUN-owned.  The SDK
ignored both return values, leaving ADC registers programmable but producing
no ADC-to-DMA samples.

MIC builds now link-wrap both calls.  The AP compatibility layer maps the two
immutable SDK calls to a single generation-scoped audio reference.  On the
first CP edge, the PM server enables module `122` and then clock `30`; on the
final edge it disables clock `30` and then module `122`.  Failures propagate
without advancing the CP reference count.  AP ELF disassembly confirmed that
`bk_aud_driver_init()` calls both wrappers, and CP ELF disassembly confirmed
the composite ordering.

The lower-half also quiesces its capture worker before pause, stop, release or
shutdown; owns partial DMA/audio setup explicitly; and returns queued buffers
only after enqueue reservations can no longer race teardown.

## T5-Board lifecycle result

A temporary, compile-gated validator used only the public NuttX audio path:
open, reserve, configure, get buffer information, create the message queue,
allocate/enqueue buffers, start, capture, pause/resume, stop, release, free,
close and reopen.  Ten consecutive cycles passed.

Observed aggregate data:

- 40 completed buffers and 12,800 stereo frames.
- Left range `-28184..32767`; right range `-32767..26279`.
- Left absolute energy `3,881,740`; right absolute energy `3,574,456`.
- 12,089 frames, or 94.45%, had different left/right samples.
- Both channels were live and non-silent; they were not mirrored.

The diagnostic build was MCUboot `18.6.51`, security counter `105`.  It was
downloaded through COM3 with the trust-chain gate and the apps-only sparse
write set.  No BL1, BL2, data, secondary-slot or calibration-tail region was
written.

## Final delivery image

All temporary ADC/DMA/IRQ snapshots were removed and
`CONFIG_BK7258_MIC_LIFECYCLE_VALIDATION` was disabled.  The signed
`t5_board_cp_app_mcuboot + t5_board_ap_app_mcuboot` production pair then built
with MCUboot `18.6.52`, security counter `106`.

- CP segment SHA-256:
  `074b6dd9bf0876b86e6098efe66f9fb51ccdb27eb28d569b522936c7948da4fd`
- AP segment SHA-256:
  `9c8f6c17130c5f4c68f8af59edf27c7bfae2b2630c14a1e821b54f31e2dc7071`
- Public trust-contract SHA-256:
  `b2a8e35256b97a0dd4fef6da253225d770001f4f20743d44bf8e9cc5f3b97c7e`

The real preflight matched the installed BL1/BL2 identities, and the loader
wrote only CP `app_crc_flash.bin@0x11000-0x3d000` plus AP
`app1_crc_flash.bin@0x165000-0x2b000`.  Canonical download logs are under
`/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260814-133056`.

After the existing BL2-to-CP release handshake, a non-halting J-Link read
reported AP boot state `READY` (`2`) with error `0`, and RPTUN state
`CONNECTED` (`4`) with error `0`.  A second read showed CP heartbeat
`0xAD -> 0xDD` and AP heartbeat `0x1A8 -> 0x220`, proving both images were
still progressing rather than exposing stale shared state.

## Boundary

This physically accepts T5-Board dual-MIC capture and repeated lifecycle.  It
does not substitute for a future analog/runtime check on a physical T5AI-Core,
nor does it authorize BL1/BL2, OTP/eFuse or calibration-tail writes.  The
diagnostic validator stays opt-in and disabled in runnable delivery profiles.
