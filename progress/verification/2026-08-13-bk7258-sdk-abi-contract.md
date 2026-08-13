# BK7258 private SDK ABI contract verification

Date: 2026-08-13 (Asia/Shanghai)

Scope: replace scattered private Beken SDK declarations and layout assumptions
with one board-owned v3.1.1.9 ABI boundary, verify the AP IPI wire contract,
and regress the result on the T5-Board. Official NuttX, apps and Beken SDK
sources were not modified.

## Result

The AP mailbox/CAN/RTC and CP Bluetooth/shared-PHY wrappers now consume one
private ABI header. Symbols, partition constants, callback-table offsets and
structure sizes that are absent from the exported SDK headers are declared and
guarded in that single location.

The IPI review warning was resolved by checking the actual v3.1.1.9 transport
instead of assuming conventional mailbox payload semantics:

- `bk_mailbox_master_send()` transports only `mailbox_data_t.param2`;
- the receive bridge reconstructs `param0` from the physical mailbox source;
- it reconstructs `param1` from the destination AP-local CPU index;
- `crosscore_mb_rx_isr()` therefore validates reconstructed route metadata,
  while the sender owns only the encoded command/sequence in `param2`.

Explicit sender writes to `param0/param1` would be discarded and would not fix
the protocol. The new helper makes the real contract visible and keeps the
logical CPU `0/1` versus physical endpoint `1/2` distinction in one place.

## Implementation boundary

- `chip/include/bk7258_sdk_abi.h` identifies the private ABI as v3.1.1.9 and
  owns the missing mailbox, CAN, RTC, Bluetooth, PHY, flash-partition,
  calibration and random-source declarations.
- Compile-time guards cover `mailbox_data_t`, endpoint values, 32-bit pointer
  width, partition and MAC-record sizes, and every callback-table offset used
  by the Bluetooth-only shared-PHY adapter.
- CAN, RTC and Bluetooth controller sources no longer carry independent shadow
  ABI definitions. Public SDK types still come from exported SDK headers.
- AP IPI receive checks use the centralized route helper. The send path remains
  intentionally `param2`-only to match the immutable transport.
- Cross-core AP/CP control structures remain in the dedicated shared SRAM
  region. AP startup already rejects a running configuration when D-cache is
  enabled without the required non-cacheable MPU region/MAIR attributes; this
  phase did not add misleading cache clean/invalidate calls to that region.

## Build and host verification

- Classic Make full dual `cp_nsh_drivercheck + ap_smp_drivercheck`: PASS.
  SDK archive checksums, generated partitions, factory layout, partition
  wrapper and ELF-checked RPTUN layout all passed.
- CMake CP drivercheck: PASS.
- CMake AP SMP drivercheck: PASS.
- `board/bk7258/tests/run_tests.sh`: RPTUN mailbox `31/31`, PM activity PASS,
  BL1 policy sanitizer PASS.
- `git diff --check`: PASS.

The signed runtime pair was built from the same source tree as
`cp_nsh_wifi_rtt_mcuboot + ap_smp_wifi_mcuboot`, version `18.6.12`, security
counter `66`. Both final generated configs retained RTT0/RTT1 support and
1024-byte up buffers.

## Physical-board regression

Target and routes:

- T5-Board;
- COM3 downloader only; COM4 was never opened;
- P0/P1 SWD;
- RTT0 NSH and RTT1 syslog.

Sparse COM3 download wrote only the executable ranges below:

| Segment | Physical offset | Length | SHA-256 |
|---|---:|---:|---|
| BL1 | `0x000000` | `0x11000` | `1423decb86f5dada877afd21470ea0b082fc1eaf551599029fd71f28ab8c67dd` |
| CP | `0x011000` | `0x10e000` | `413472119e8898c2c4475a6aa66ef0fc4fa753d17281649bffb69afe6225006f` |
| AP | `0x165000` | `0x40000` | `6ab90302a412949b0707480e5d10f256baabc1731fe012f02287c2c7048f6c51` |
| primary BL2 | `0x51d000` | `0x4000` | `f88d62c229b6ba016612011ae5d0154069997af8242bdbde2bf4b501f9ff8282` |
| secondary BL2 | `0x53f000` | `0x4000` | same as primary BL2 |

All five operations reported `WriteFlash -> pass`, followed by
`Writing Flash OK` and `All Finished Successfully`. LittleFS, `usr_config`,
the calibration tail, OTP and eFuse were outside the write ranges. Canonical
download log:
`/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260813-150348`.

J-Link at 100 kHz identified STAR over P0/P1. Before release it observed the
BL2 hold point at `PC=0x280205a2`, `VTOR=0x28020000`; it wrote only `JLNK`
(`0x4a4c4e4b`) to `0x2809f7f0`. In the running image it observed:

```text
VTOR=0x28010800
RTT control block=0x2802b9a0 (3 up / 3 down channels)
RTT0 up=1024 bytes, RTT1 up=1024 bytes, RTT0 down=128 bytes
P0/P1 function=0x22
P0/P1 control=0x00050048 / 0x00050048
```

RTT0 `apctl status` reported:

```text
AP state=READY, error=0, generation=1
RPTUN state=CONNECTED, error=0, flags=0x0001ffff, pending=0/0
supervisor state=HEALTHY, reason=NONE, faults/recoveries/consecutive=0/0/0
CPU2 state=SCHEDULER_ONLINE, error=0, online mask=0x3
```

Two non-halting shared-SRAM snapshots then supplied the cross-core evidence:

| Contract | First -> second observation | Error evidence |
|---|---|---|
| AP boot | heartbeat `0x11dd -> 0x11f1` | READY, error 0 |
| CPU2 | heartbeat `0x23de -> 0x2404`; SMP calls `0x11f6 -> 0x1209` | SCHEDULER_ONLINE, error 0 |
| AP IPI | CPU1 IRQ/wake `0x11f5 -> 0x1208` | duplicate/lost/send/spurious/stale all 0 |
| AP SMP | tx/rx/handler `0x11f5 -> 0x1208` | PASSED, online mask `0x3`, send failures 0 |
| RPTUN | CP RX `0x55a -> 0x560`; AP RX `0x52d -> 0x533` | CONNECTED, pending `0/0`, error 0 |

The AP boot structure also published the expected non-cacheable contract:
`RBAR=0x2800001a`, `RLAR=0x3fffffe3`, with the runtime contract flags set.
This demonstrates live bidirectional progress rather than a one-time boot
snapshot and directly covers the IPI/cache-visible paths called out by review.

The CP Bluetooth initialization state showed a valid non-fallback base address,
controller IPC ready, MAC ready, PHY adapter ready, Wi-Fi controller dependency
ready, one vendor-init call and vendor result zero. A later direct J-Link write
to RTT0's cacheable down ring was not consumed; no cache-forcing debug write was
attempted because the controller state and cross-core evidence were already
sufficient and the target was left running.

## Evidence boundary

- This closes the centralized private ABI boundary and the current AP IPI
  route-contract concern for SDK v3.1.1.9.
- It does not claim that every private SDK ABI is now public or version-stable;
  new private dependencies must be added to the same guarded header.
- It does not implement complete SDK coordinated low-voltage PM, broad wrapper
  lifecycle repair, or a bringup init-table/rollback framework. Those remain
  separate phases.
- No QEMU result is used as evidence for cache visibility, interrupt timing or
  cross-core progress; those claims above come from the physical board.
