# CP 649 startup repair candidate

Status: the P0 target build is verified; the post-P0 soft-off WFI wrapper is
a source/linker candidate pending a fresh target build. **New boot and K2
operation are not verified**.
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
| copied `.data` (includes selected executable inputs) | 27004 | 25404 |
| `.bt_spec_data` | 19388 | 19388 |
| `.bss` | 160544 | 160544 |
| interrupt stack | 2048 | 2048 |
| startup/idle stack | 2048 | 2048 |
| initial heap, before allocator overhead | 50780 | 52380 |

The 649 failure reports allocator arena 50776, used 50728, free/max-free 48
and a failed 2048-byte allocation. The existing `WIFI MALLOC FAIL` label is
from a global malloc wrapper: it does not identify the caller as Wi-Fi.
The candidate adds the caller PC and zero-initialization scope to the first
failure diagnostic. Without new boot evidence the exact old failed caller
remains unknown. The PSRAM heap control object is 376 bytes in this pinned
binary; it must remain in internal SRAM for hardware atomic operations.

## Budget and initialization order

The final wrapper adds 64 bytes to the first narrow candidate; the resulting
1600-byte static saving alone does not prove a safe boot budget.
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
control initialization after the OTA stage. `run-platform` passed. The updated
build-workspace suite passed (9 tests), including the soft-off copied-symbol
fixture. No new test file/framework was introduced.

## Sleep residency boundary

The pinned PM archive combines deep sleep and low voltage in one indivisible
`.itcm_sec_code` section. Retain that section and the specific analog-register,
clock, SPI-latch and GPIO helpers called after high-frequency clock disable;
leave unrelated setup code and read-only data in Flash. Function-local literal
pools stay with their input sections. The architecture WFI section is copied
to SRAM too. The reset copy uses `_eronly` and `_sdata.._edata` as before.

The public build writes `cp-memory-report.json`, bound to ELF/config hashes,
and rejects a missing or non-copied implementation of each selected helper.
For a soft-off CP it now also requires `__wrap_arch_deep_sleep` and
`bk7258_pm_soft_off_wfi_reset` in the copied range. Same-named local SDK
helpers in other objects are not mistaken for that copy. This symbol gate
supplements, not replaces, call/veneer disassembly review.

The locked SDK has one unresolved `R_ARM_THM_JUMP24 arch_deep_sleep` relocation
from `sys_pm_hal.c.obj`; its deep path tail-branches there, and the locked
implementation executes `WFI; ISB; BX LR`. The soft-off candidate wraps that
symbol only when soft-off is configured. Its boot-only SRAM latch selects a
SRAM wrapper at the actual WFI boundary: it changes the mask representation to
`PRIMASK=1` and `BASEPRI=0`, then after wake issues the standard NuttX AIRCR
`SYSRESETREQ` sequence from SRAM and cannot return to the SDK XIP caller.
Low-voltage callers see an unarmed latch and call the real function, retaining
their existing return path. No code is placed in sleep-inaccessible PSRAM.

This is a closure candidate for the known mask and XIP-return hazards, not a
K2 acceptance claim. The fresh target link (`reset-wfi-integration-build-2.log`,
exit 0) places the wrapper at `0x28011168` and reset at `0x28011148`, inside
`_sdata=0x28010940 .. _edata=0x28016c7c`. Disassembly shows the SDK deep
tail branch and low-voltage call both target the wrapper. The armed path after
WFI only calls the SRAM reset sequence; its literal and rejected-reset loop
remain in SRAM. The first link deliberately failed the new location gate:
the team object is `.c.o`, not the SDK `.c.obj` spelling. Correcting that input
selector fixed residency rather than weakening the assertion.
The scope of `SYSRESETREQ` across the chip, clock restoration, and actual wake
behavior still require board evidence. The table above reflects this linked
candidate, whose ELF SHA256 is
`cdc6b7c99696f313252f33cc3f156fb5deb3ad5452ad4f3d395d8695f78fdedd`.

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
