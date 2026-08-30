# BK7258 SARADC and T5-Board ADC-key baseline verification

Date: 2026-08-15

## Conclusion

`PARTIAL`: the generic BK7258 SARADC controller, CP/AP IPC path, NuttX ADC
upper/lower-half integration, ADC14 released baseline and complete cleanup are
physically verified.  The board endpoint remained released for this run, so
the expected active-low SW5 transition deliberately timed out.  This record
does not claim a physical ADC-key PASS until a fresh run observes
released -> pressed -> released.

## Chip and board contract

- The AP chip layer publishes `/dev/adcN` and contains no T5, P12, key or
  button assumption.  The validation sampler uses only `open`,
  `ANIOC_RESET_FIFO`, `ANIOC_TRIGGER`, `read(struct adc_msg_s)` and `close`.
- One open session owns the selected channel's GPIO mapping.  Each trigger
  takes the shared ADC controller, initializes and explicitly configures it,
  starts one bounded conversion through the supported `bk_adc_read` mailbox
  operation, then stops, deinitializes and releases it before delivering the
  sample to the NuttX upper-half FIFO.
- The pinned AP/CP objects both use the Arm small-enum ABI.  The final build
  gates `adc_config_t` at 36 bytes with enum offsets 16/17/18/19 and
  `vol_div` offset 34; the NuttX `adc_msg_s` delivery is 5 bytes.
- The explicit conversion tuple is clock `0x0030a0c5`, sample rate/filter 0,
  steady control 7, continuous conversion mode, 26 MHz XTAL source, selected
  channel, saturation mode 3 and no voltage divider.  Unused pointer/length
  and calibration-result fields are zeroed before the SDK call.
- The CP SARADC server initializes the boot-lifetime shared GPIO runtime
  before the ADC server.  The NuttX SDK IRQ bridge owns the vendor interrupt
  registration symbol; the board GPIO lower half is not a hidden dependency.
- The selected T5-Board binding owns the physical fact P12 = ADC14.  SW5 is
  active-low: released is pulled toward 3.3 V and pressed is pulled to ground.
  UART0 without flow control remains on P10/P11 and does not claim P12.

## Signed host artifact

This `18.6.77` artifact records the SARADC source state later committed as
`c67fac3`, based on `a512602` before the DAC-EQ merge.  The SARADC profile does
not enable Audio or DAC-EQ, and later integration does not retroactively
change the hashes below.

The clean paired Classic Make build completed with:

- MCUboot version `18.6.77+0`;
- protected CP/AP security counter `0x1206004d` (`302383181`);
- CP `t5_board_cp_saradc_key_validation_mcuboot`;
- AP `t5_board_ap_saradc_key_validation_mcuboot`;
- compatibility `t5_board_saradc_key_validation_mcuboot_v1`;
- CP/AP pinned SDK bundle `v3.1.1.9` with manifest and provenance checks.

Key artifact SHA-256 values:

| Artifact | SHA-256 |
|---|---|
| CP ELF | `55821186f85bf3196bb5f53524b84e45a2de1495bcc2690ab822462acb2517da` |
| AP ELF | `8b9b39494769102cb990f9725191fba4f4623c882e548e625f28f2ddd07bf98a` |
| CP signed image | `e2546dd2a348c30309f2161b4022026845345fe75fab2fc601c0ee6497de1080` |
| AP signed image | `2f05dc299aeb71c966aa1746bbdde7543a0d1bba0d2939925fc89f92bf84de84` |
| CP apps-only Flash image | `2588af6f15238c80c09122b24ea928698e2990d0f764f8d669edd86e134ecb9a` |
| AP apps-only Flash image | `2bb2dd00e55418f6d284b788c92252412f4e2ca973a99134ad52ba5a4fddd619` |
| public trust contract | `1873b4e00137cc5ee19fdae55f246a8d754e60d4bd85d82d2b93cfa3d7f75fc2` |

Final link evidence includes:

- CP `bk_int_isr_register` from `libarch.a(bk7258_sdk_irq.o)`;
- CP `bk_adc_driver_init` from the CP v3.1.1.9 `adc_driver.c.obj`;
- AP `adc_register` from NuttX `libdrivers.a(adc.o)`;
- AP `bk_adc_read` from the AP v3.1.1.9 `saradc_client.c.obj`;
- no linked `bk_adc_single_read`, whose immutable server operation is absent;
- generic driver diagnostic `0x28051874/0x74` and validation diagnostic
  `0x28053bd8/0xdc`.

The dual-image gate also rejects an AP client without the CP server, the
dedicated profile without its exact validation/MCUboot compatibility
contract, and every known T5 P12 conflict: UART0 flow control, CP GPIO
lower-half/tests, TOUCH0 and AP USB0.

### Post-EQ integration build

After rebasing the SARADC change onto merged DAC-EQ commit `4248440`, a clean
paired Classic Make build completed as MCUboot version `18.6.79`.  The final
SDK manifest/provenance, profile, partition, required/forbidden ELF symbol,
map-owner, signing, factory-layout and RPTUN-layout gates all passed.  Key
host-only artifact hashes were:

| Artifact | SHA-256 |
|---|---|
| CP ELF | `569780f1eb97983ee4b59be038aac46797db19999278efb2bd4479d3460c162b` |
| AP ELF | `8b9b39494769102cb990f9725191fba4f4623c882e548e625f28f2ddd07bf98a` |
| CP signed image | `311fab1762bf02d12c67c20a556a229ad4377222c274669442ab9dab3310e464` |
| AP signed image | `a65aab6795107e3602a211947a6229ec99ff1a2f446b67116ed45f280b0a360d` |

This integration image was not downloaded.  The physical conclusion below
remains bound to the `18.6.77` run and remains `PARTIAL`.

## Apps-only download

The non-halting preflight matched all packaged BL1/BL2 public identities
before the loader ran.  COM3 wrote only:

- CP `[0x011000, 0x042000)` from the `0x31000`-byte Flash image;
- AP `[0x165000, 0x185000)` from the `0x20000`-byte Flash image.

BL1, BL2, manifests, slot B, LittleFS, configuration, calibration, OTP/eFuse
and lifecycle state were not written.  The target was then observed at the
preserved BL2 hold (`VTOR=0x28020000`, release token zero) before the RAM-only
`JLNK` token started the pair.

## Released-baseline hardware result

SW5 was intentionally left released.  The terminal validation diagnostic was:

- magic/version/size `SADV`/1/`0xdc`;
- state `FAILED`, result `-ETIMEDOUT`, stage `DONE`;
- binding `TKEY`, expected channel 14, active direction low;
- one open and one close, two FIFO resets;
- 1,565 triggers, reads, samples and upper-half deliveries;
- all 1,565 messages reported channel 14;
- released baseline 64 samples: minimum 8,121, maximum 8,190, median 8,189;
- computed active delta 6,551 and release tolerance 818;
- transitions 0 and timeout count 1, as expected without a press;
- open/ioctl/read/short-read/channel/range errors all zero;
- final driver state `RESET`, resources zero and first error zero;
- setup/shutdown and GPIO map/unmap 1/1;
- acquire/release, init/deinit, config/bypass, start/stop and
  trigger/sample/deliver each 1,565.

AP heartbeat advanced from `0x17d` to `0x5b2`; the RPTUN CP/AP heartbeats
advanced from `0x9c/0x17d` to `0x252/0x5b2` while the validator sampled.  The
timeout is therefore the physical-transition gate, not a controller, IPC,
FIFO, lifecycle or liveness failure.

## Remaining physical gate and boundary

A complete PASS requires a fresh boot with SW5 initially released, held while
the validator collects the active phase, and released again for the final
phase.  The terminal record must report `PASSED`, result zero, two transitions,
released/high -> active/low -> released/high medians, zero errors and zero
driver resources.

This phase covers one external channel, synchronous single-trigger reads and
one bounded lifecycle.  It does not establish absolute voltage accuracy,
other channels, continuous/high-rate sampling, battery measurement,
temperature-service coexistence, PM coexistence or long-duration behavior.
