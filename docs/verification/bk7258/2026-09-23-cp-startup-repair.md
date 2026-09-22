# CP 649 startup repair candidate

Status: target build verified; **new boot and K2 operation not verified**.
No board reset, Flash write or soft-off was performed for this investigation.
649 remains a diagnostic image, not a recommended deployment.

## Inputs and comparison

Base: `f79321a047d7d8e9e6a7182f1484f9d4b9b05cb8`, with the scoped
linker/platform/diagnostic/build-verifier changes accompanying this document.
The `1b751b9f` App/network/configuration changes remain present. Agent stays
at `f116e08ae3f97181b8162a181d56ef5d8944f7ae`.

The frozen 649 CP ELF SHA256 is
`6f67818723b8edb5f78891247446553f40967a41d43d982b4c6089fecd80c07e`.
No distinct retained 648 ELF/map was found in the bounded evidence locations;
the table compares real 649 and repair ELFs, not a reconstructed 648 binary.

| Bytes | 649 | Repair candidate |
| --- | ---: | ---: |
| `.text` (includes read-only inputs) | 1052128 | 1051968 |
| `.pm_setup` in Flash | 0 | 1680 |
| copied `.data` (includes selected executable inputs) | 27004 | 25340 |
| `.bt_spec_data` | 19388 | 19388 |
| `.bss` | 160544 | 160544 |
| interrupt stack | 2048 | 2048 |
| startup/idle stack | 2048 | 2048 |
| initial heap, before allocator overhead | 50780 | 52444 |

The 649 failure reports allocator arena 50776, used 50728, free/max-free 48
and a failed 2048-byte allocation. The existing `WIFI MALLOC FAIL` label is
from a global malloc wrapper: it does not identify the caller as Wi-Fi.
The candidate adds the caller PC and zero-initialization scope to the first
failure diagnostic. Without new boot evidence the exact old failed caller
remains unknown. The PSRAM heap control object is 376 bytes in this pinned
binary; it must remain in internal SRAM for hardware atomic operations.

## Budget and initialization order

The 1664-byte static saving alone does not prove a safe boot budget.
In the Wi-Fi + PSRAM profile, PSRAM/private-heap/system-region setup now runs
after Wi-Fi RF calibration but before Bluetooth IPC allocation. Bluetooth
explicitly depends on that stage. BT-only profiles retain calibration-before-
PSRAM ordering. No SRAM boundary or necessary stack was reduced.

Pinned `bt_ipc_init` requests a 64 x 8-byte queue, a 2048-byte worker stack,
and a semaphore. The queue adapter adds 92 bytes; the semaphore is the existing
static 28-byte BT-init semaphore. Thus at least 2652 bytes of dynamic requests
plus task/allocator overhead move after heap expansion. This is not a measured
high-water mark or a guarantee about the largest free block.

Existing platform scenarios cover order and idempotency, including Wi-Fi
control initialization after the OTA stage. `run-platform` passed. The build
workspace tests passed (8 tests). No new test file/framework was introduced.

## Sleep residency boundary

The pinned PM archive combines deep sleep and low voltage in one indivisible
`.itcm_sec_code` section. Retain that section and the specific analog-register,
clock, SPI-latch and GPIO helpers called after high-frequency clock disable;
leave unrelated setup code and read-only data in Flash. Function-local literal
pools stay with their input sections. The architecture WFI section is copied
to SRAM too. The reset copy uses `_eronly` and `_sdata.._edata` as before.

The public build now writes `cp-memory-report.json`, bound to ELF/config hashes,
and rejects a missing or non-copied implementation of each selected helper.
Same-named local SDK helpers in other objects are not mistaken for that copy.
This symbol gate supplements, not replaces, call/veneer disassembly review.

Important remaining K2 boundary: the SDK tail-branches into `arch_deep_sleep`,
whose WFI may return to an XIP caller. Unexpected return, exception paths and
actual wake behavior still require closure; this report does **not** declare
soft-off fixed. No code was placed in sleep-inaccessible PSRAM.

## Executed target build

```
python3 tools/bk7258/bk7258.py build --board aidk_ai_toy --boot mcuboot --development-identity --rollback-floor 649 --jobs 4
```

Exit 0, CP/AP/BL1/BL2 built by the public entry. Floor 649 here is an explicit
diagnostic build input, not a new release counter or authorization to install.
Local logs: `out/shaniu-repair-20260923/p0-build-budget.log`,
`p0-platform-tests.log`, `p0-build-tests.log` (workspace-relative).
The build consumes team sources through `vendor/beken/chips/bk7258` and uses
the pinned GCC 10.3-2021.10 and cp-aidk SDK v3.1.1.9 inputs.
The same public command with `--board t5_board` also completed CP/AP/BL1/BL2
with exit 0 (`p0-t5-build.log`). T5 does not enable soft-off relocation but
does share the combined-radio initialization sequence. This is a build check,
not T5 physical acceptance.
Final release hashes must come from the final build manifest, not this mutable
development output directory. A clean final target build remains a separate gate.
