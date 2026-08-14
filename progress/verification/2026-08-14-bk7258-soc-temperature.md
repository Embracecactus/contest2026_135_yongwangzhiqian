# BK7258 on-die temperature verification

Date: 2026-08-14

## Conclusion

PASS.  The T5-Board completed eight real AP-to-CP temperature requests with
valid BK7258 on-die SARADC raw codes.  The default configuration deliberately
does not publish an absolute Celsius value because no trusted per-device 25 C
reference was supplied.

## Implemented contract

- CP owns the immutable v3.1.1.9 SDK `temp_detect_get_temperature()` call and
  executes the potentially blocking conversion from LPWORK rather than from an
  RPMsg callback.
- AP and CP use a project-owned 32-byte RPMsg message with generation and
  sequence identity, bounded waits, duplicate coalescing and committed-reply
  replay.
- Endpoint creation, send and teardown are serialized on both cores.  Reply
  tuple validation and publication are atomic, and a disconnect/reconnect
  cannot turn a stale semaphore wakeup into a successful request.
- Temperature sampling holds a CP PM activity vote.  The coordinated-standby
  admission path rechecks PM and RPTUN idleness after the mailbox wait, closing
  the interrupt-unmask admission window.
- AP always exposes authoritative raw data.  NuttX thermal-zone registration
  is gated by an explicit, per-device raw reference measured at 25 C.
- A bounded validation worker records eight samples in a debugger-visible
  diagnostic structure and never blocks the RPTUN receive callback.

## SDK contract audit

The CP SDK bundle defines `CONFIG_SOC_BK7236XX` and does not enable
`CONFIG_SDMADC_TEMP`.  The selected v3.1.1.9 implementation therefore uses:

- valid raw range `11..1364` (the SDK accepts values strictly greater than 10
  and strictly less than 1365);
- slope magnitude 46 ADC codes per 10 C.

The direction is also verified from the immutable SDK rather than assumed.
The PHY calibration binary's `manual_cal_bk7236.c.obj`, function
`manual_cal_set_tmp_pwr()`, computes a value above 25 C when the current raw
code is below its saved reference, and below 25 C when the raw code is above
the reference.  The Beken v3.1.1.9 and Tuya SDK binaries independently use the
same sign.  The AP conversion consequently uses
`25 C + (reference_raw - raw_code) * 10 C / 46`.

This audit corrected the earlier provisional constants 51 and 12 before the
current build and board test.

## Host verification

- Profiles: `t5_board_cp_app_mcuboot` and
  `t5_board_ap_temperature_validation_mcuboot`.
- The exact profile-pair preflight passed and now rejects a one-sided
  temperature client/server configuration.
- A clean signed dual-image build completed with version `18.6.62` and
  security counter `116`.  SDK checksum, partition, Factory and RPTUN layout
  checks passed.
- CP ELF contains the temperature server, standby-idle hook and the real SDK
  `temp_detect_get_temperature` symbol.  AP ELF contains the client, validator
  and `g_bk7258_temperature_validation_diag` at `0x28051100`.
- Public trust-contract SHA-256:
  `b2a8e35256b97a0dd4fef6da253225d770001f4f20743d44bf8e9cc5f3b97c7e`.
- CP application Flash segment:
  `app_crc_flash.bin@0x11000-0x3e000`, SHA-256
  `2a72ce22831a5ec3f67fc9c144a5cf76ad3449e1d859b32ca7e8948128e60c0e`.
- AP application Flash segment:
  `app1_crc_flash.bin@0x165000-0x1d000`, SHA-256
  `664704294e39e250438191f1f4848c914ba079ef147407a76fe123fedf6226de`.
- Shell syntax, profile metadata preflight and `git diff --check` passed.

## Download result

The fail-closed J-Link preflight matched the installed BL1/BL2 public
fingerprints before `bk_loader` was invoked.  The apps-only COM3 path then
wrote exactly the two CP/AP segments listed above.  Both erase/write operations
passed and the loader rebooted the board.

BL1, BL2, manifests, slot B, LittleFS, vendor configuration and calibration
partitions were preserved.  No OTP/eFuse/lifecycle state was read or written,
and COM4 was never opened.  The existing debug hold was released only through
its explicit `JLNK` continuation token.

Download evidence is archived under the workspace log directory:
`logs/bk7258-auto-debug/20260814-224659`.

## T5-Board measurement

The non-halting J-Link read of `g_bk7258_temperature_validation_diag` returned:

```text
magic               0x504d5442
version             1
state               2 (PASS)
status              0
generation          1
successful_samples  8
failed_samples      0
minimum_raw         569
maximum_raw         569
last_raw            569
last_millicelsius   0
last_flags          0x1 (RAW_VALID)
```

The eight values are real on-chip measurements, not a scripted fixture.

## Boundary

This closes raw on-die temperature acquisition, AP/CP transport, disconnect
recovery, PM exclusion and calibration-gated NuttX thermal integration.  It
does not establish this device's 25 C calibration reference.  Until a trusted
per-chip reference is supplied, consumers must use `raw_code` and must not
present `temperature_millicelsius` as an absolute temperature.
