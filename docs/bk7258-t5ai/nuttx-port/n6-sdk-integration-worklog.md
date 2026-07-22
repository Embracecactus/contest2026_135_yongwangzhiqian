# BK7258 T5-AI N6 SDK Integration Worklog

> **Current Stage:** N6 — Beken SDK integration, WDT handoff, and IRQ adaptation
> **Current status:** A1 80-slot RAM-vector board verification complete on 2026-07-22. A1 is `board-verified`. UART/boot, N4 DVFS tier 5/320 MHz, ABWTK/NSH/UART RX/WDT stability, LittleFS, VTOR-to-RAM, flash magic slots 64/65, RAM slots 15/31/64/65 all PASS. SDK IRQ bridge prerequisite satisfied but bridge NOT started; requires explicit user authorization. No flash, no commit, no push.
> **Last updated:** 2026-07-22

## Documentation rule

Every substantive new change or result in this Stage must be recorded here before proceeding to the next technical step. Each update should include the change, evidence, status, conclusion, current diagnostic state, and next single minimal action. A board result that disproves an older conclusion must explicitly retract the stale conclusion.

## 2026-07-21 — WDT reset and SysTick IRQ root cause

### Objective

Replace temporary raw-register watchdog shutdown with the intended architecture:

```text
NuttX watchdog upper-half
  -> BK7258 lower-half wrapper
  -> Beken SDK bk_wdt_* / bk_aon_wdt_* APIs
```

NuttX automonitor owns periodic keepalive. The wrapper contains no raw WDT register writes.

### Effective watchdog configuration

```text
CONFIG_WATCHDOG_AUTOMONITOR=y
CONFIG_WATCHDOG_AUTOMONITOR_BY_WDOG=y
CONFIG_WATCHDOG_AUTOMONITOR_TIMEOUT=8
CONFIG_WATCHDOG_AUTOMONITOR_PING_INTERVAL=2
```

Expected runtime chain:

```text
SysTick (10 ms)
  -> systick_interrupt()
  -> nxsched_process_timer()
  -> wd_timer()
  -> watchdog_automonitor_wdog() every 2 s
  -> lower-half keepalive()
  -> bk_wdt_feed()
```

### Controlled SDK WDT initialization

Current overlay sequence in `board/bk7258_t5ai/chip/bk7258_wdt.c`:

```text
bk_timer_driver_init()
bk_wdt_driver_init()
bk_timer_stop(TIMER_ID2)
bk_aon_wdt_stop()
watchdog_register("/dev/watchdog0", ...)
```

The SDK initializes its internal WDT state, TIMER_ID2 is then stopped so NuttX automonitor owns periodic feeding, and the bootloader AON watchdog is stopped through the SDK API.

### Diagnostic markers

Temporary markers were introduced to isolate the failure:

| Marker | Meaning |
|---|---|
| `A` | immediately before `watchdog_register()` |
| `B` | registration, timeout setup, and lower-half `bk_wdt_start()` returned |
| `W` | direct one-tick NuttX `wd_start()` probe was accepted |
| `T` | the one-tick WDog probe callback executed |
| `K` | automonitor first called lower-half `keepalive()` and `bk_wdt_feed()` |
| `s` | temporary direct SysTick vector-entry probe executed |

### Rejected hypotheses

#### SDK start or registration hangs

Board result:

```text
AB
NuttShell (NSH)
...
HF
```

`B` proved `watchdog_register()` and SDK `bk_wdt_start()` returned. The failure was after registration.

Status: `rejected`.

#### Global PRIMASK or BASEPRI remains disabled

Forcing `up_irq_enable()` and then `up_irq_restore(0)` did not produce `K`.

Status: `rejected`; both forced-mask experiments were removed.

#### `wd_start()` rejects the automonitor event

A direct one-tick WDog probe produced:

```text
ABW
```

`W` proved insertion succeeded, but neither its `T` callback nor automonitor `K` ran.

Conclusion: the NuttX WDog queue was populated but not serviced.

Status: `board-verified` observation.

### SysTick hardware-entry probe

The existing temporary vector probe was connected directly to vector slot 15. Board result:

```text
sABW
NuttShell (NSH)
...
HF
```

` s ` proved all of the following:

- SysTick counter and interrupt generation work;
- TICKINT is enabled;
- VTOR points to the expected vector table;
- vector slot 15 is fetched correctly;
- Cortex-M33 can enter and return from the SysTick exception.

This excluded the SysTick hardware setup and global interrupt masks. The failure boundary moved to the NuttX dispatch path beginning at `exception_direct`.

Status: `board-verified`.

### Final SysTick change

Changed only vector slot 15:

```c
[15]         = &exception_common;
[16 ... 63] = &exception_direct;
```

Board result:

```text
ABWT
NuttShell (NSH)
nsh> [ipc_svr]
create_socket failed.
K
```

The board remained running and no longer entered the 8-second reset loop.

Status: `board-verified`.

### Confirmed root-cause boundary

> On the current BK7258 port, SysTick routed through `exception_direct` did not complete NuttX tick/WDog processing. Routing SysTick through the full context-saving `exception_common -> arm_doirq -> irq_dispatch` path restored `nxsched_process_timer()`, the WDog queue, watchdog automonitor, and `bk_wdt_feed()`.

The exact failing instruction or ABI assumption inside the optimized `exception_direct` path has not been isolated and is not required for the current functional recovery.

The shared diagnostic `HF` marker is used by both vector slot 2 (NMI) and slot 3 (HardFault). In this failure chain it was consistent with WDT expiry entering NMI; it was not proof of an ordinary software HardFault.

## 2026-07-21 — BK7236N IRQ reference comparison

Reference files under `$VENDOR_BEKEN/chips/bk7236n/` show three missing or incorrect parts in the BK7258 overlay.

### 1. Exception entry choice

`beken_head.S` routes SysTick and defined external IRQ handlers to `exception_common`, not `exception_direct`:

```asm
handler_name:
    b exception_common
```

This matches the BK7258 board result above.

### 2. SDK-to-NuttX IRQ bridge

`beken_interrupt_base.c` reimplements `bk_int_isr_register()` in the NuttX adaptation layer:

```text
SDK interrupt source
  -> map to NuttX IRQ number
  -> irq_attach(wrapper)
  -> up_enable_irq()
  -> wrapper calls the SDK ISR callback
```

The BK7258 overlay currently lacks an equivalent bridge. The precompiled SDK implementation registers callbacks in its private `s_irq_handler[]` and enables NVIC directly; NuttX `irq_dispatch()` does not automatically consult that private table.

This remains a likely blocker for SDK Timer/UART and later SDK modules.

Status: `static-only`; not yet board-verified as a complete fix.

### 3. IRQ count definition — superseded finding

BK7258 currently defines:

```c
#define NR_IRQS 48
```

The initial comparison incorrectly assumed that BK7258 had only 48 external
IRQs and proposed `NR_IRQS=64`. Targeted BK7258 SDK inspection later proved
that its ICU map, fixed handlers and private callback table cover external
NVIC indices 0 through 63. Therefore the complete logical NuttX space would
normally be 80 IRQ numbers, not 64.

The current value is still incomplete, but it must not be corrected in
isolation because mandatory boot magic occupies vector slot 64. See the later
"Targeted SDK IRQ and multicore evidence" section.

Status: earlier 48-external proposal `rejected`; full correction `blocked` on
runtime vector layout design.

## 2026-07-21 — UART IRQ step 1: full exception entry

### Change

Changed the BK7258 custom vector table in
`board/bk7258_t5ai/chip/bk7258_vectors.c` from:

```c
[15]         = &exception_common;
[16 ... 63] = &exception_direct;
```

to:

```c
[15 ... 63] = &exception_common;
```

The now-unused `exception_direct` declaration and stale vector-table comments
were removed or updated. Slots 2 and 3 remain connected to the temporary
`bk7258_hardfault_handler`; slots 4 through 63 now use `exception_common`.

### Rationale and evidence boundary

The preceding SysTick experiment board-verified that the complete
`exception_common -> arm_doirq -> irq_dispatch` path restores NuttX interrupt
processing. The BK7236N reference also routes its external IRQ handlers through
`exception_common`. This change applies the same entry path to all 48 BK7258
external vector slots so the already attached NuttX UART1 ISR can receive RX
interrupt dispatch.

Status: `static-only`.

This source change has not yet been built or board-tested. It does not yet
prove UART RX works, and it does not correct the independent `NR_IRQS=48`
definition or add the SDK-to-NuttX IRQ bridge.

Current diagnostic state: the WDT `A/B/W/T/K` markers and unused SysTick probe
remain unchanged so the next board run can simultaneously detect a WDT
regression.

Next single minimal action: build this vector-only change, then flash and test
UART RX at the NSH prompt. If boot, WDT stability, or UART behavior regresses,
restore only external slots 16 through 63 to `exception_direct` before further
IRQ changes.

## 2026-07-21 — UART RX external-IRQ path board verification

### Board evidence

After building and flashing the external-vector change, the board reached NSH
with the expected watchdog markers:

```text
ABWT
NuttShell (NSH)
nsh> [ipc_svr]
create_socket failed.
K
```

UART input was accepted repeatedly and NSH executed commands:

```text
nsh> ls
/:
 data/
 dev/
 proc/

nsh> cat /data/probe.txt
BK7258LFS-OK
```

### Conclusion

External vector slots 16 through 63 using `exception_common` restore the
NuttX UART1 RX interrupt path. UART RX and command dispatch are therefore
`board-verified`. The final shown boot also retained watchdog automonitor
feeding (`T`/`K`) and LittleFS access to the persistent probe file.

The log contains a transition from an earlier boot to a second boot immediately
after `K`; without reset-cause attribution it is not used as evidence of either
a spontaneous watchdog reset or a manual reset. The sustained second boot is
the UART RX verification point.

Status: `board-verified`.

This result verifies the exception-entry choice for the existing NuttX UART
ISR. It does not fix the undersized NuttX IRQ-number space and does not prove
that precompiled SDK modules using the SDK-private ISR table can dispatch
through NuttX.

Current diagnostic state: external slots 16 through 63 remain on
`exception_common`; `A/B/W/T/K` and the unused SysTick probe remain pending
final cleanup.

Next single minimal action: correct the IRQ count split to 48 external IRQs and
64 total NuttX IRQ numbers, then rebuild and board-test the same UART/WDT
baseline before implementing the SDK IRQ bridge.

## 2026-07-21 — Tri-core SMP/AMP design boundary

The current verified port runs one NuttX instance on CPU0; therefore the
current vector, NVIC, UART and SDK IRQ bridge work is scoped to CPU0. A
three-core chip does not by itself mean that the current NuttX configuration
is SMP. Other cores running separate SDK firmware would be an AMP arrangement
and must retain their own local interrupt handling, with cross-core events
using the chip IPC/mailbox path rather than CPU0 `g_irqvector[]`.

The CPU0 bridge should nevertheless avoid UP-only assumptions: protect shared
registration state with a spinlock rather than only local interrupt masking,
separate one-time callback-table initialization from per-core NVIC setup, and
make interrupt ownership/routing explicit. It must not silently enable the
same peripheral IRQ on multiple cores.

A future NuttX SMP port would be a separate architecture milestone requiring
secondary-core startup, per-core VTOR/IRQ-stack setup, inter-processor
interrupts, scheduler SMP hooks, per-core timer policy, interrupt affinity and
cache/coherency validation. The SDK IRQ bridge alone does not provide those
facilities and must not be described as tri-core SMP completion.

Status: `static-only` design constraint. Preserve room for later per-core
routing rather than mixing SMP bring-up into the current UART/WDT recovery
stage.

## 2026-07-21 — Targeted SDK IRQ and multicore evidence

The BK7258 SDK does not use the earlier assumed 48-entry external IRQ model.
Targeted source inspection established the following static facts:

- `middleware/soc/bk7258/soc/icu_map.h` maps 64 SDK interrupt sources
  one-to-one to external NVIC indices 0 through 63. `INT_SRC_MAILBOX` maps to
  external IRQ 63.
- `middleware/driver/bk7258/interrupt.c` defines handlers for external IRQs
  0 through 63. Each fixed vector handler calls `arch_interrupt_get_handler()`
  and then invokes the callback stored for that NVIC index.
- `middleware/arch/cm33/arch_interrupt.c` owns a local
  `s_irq_handler[64]`. Registration stores a callback, sets priority and
  enables that local NVIC IRQ; unregistration disables it and clears the
  callback. The SDK `bk_int_isr_register()` path ignores its `arg` parameter.
- SDK startup sources provide separate CPU0, CPU1 and CPU2 vector tables. The
  inspected BK7258 library configuration sets `CONFIG_CPU_CNT=2`, while
  optional `CONFIG_SOC_SMP` paths and AMP resource/mailbox code coexist.
- Under `CONFIG_SOC_SMP`, mailbox channels are explicitly hard-bound to a
  processor through `sys_drv_core_intr_group2_enable()`. AMP resource sharing
  uses mailbox IPC and per-CPU ownership/counting.

This overturns the previous proposed constants. A complete NuttX logical IRQ
space for 64 external IRQs would normally require:

```text
BK7258_EXTERNAL_IRQS = 64
NR_IRQS = 16 + 64 = 80
```

However, the current bootable NuttX image places mandatory bootloader magic at
image offset `0x100`, which is vector slot 64. A contiguous 80-entry runtime
vector table would need slots 64 through 79 for external IRQs 48 through 63,
so simply changing `NR_IRQS` to 80 would overwrite or conflict with the boot
magic layout. The current table only provides external IRQs 0 through 47;
UART1 works because it is external IRQ 15.

Status: `static-only`, newly identified blocker. The earlier
"48 external IRQs / `NR_IRQS=64`" correction is `rejected` and must not be
implemented.

Next single minimal action: reconcile the vendor boot image/vector layout with
its 64-entry runtime IRQ table and determine whether NuttX must keep a minimal
boot table at the image origin and switch VTOR after reset to a separate full
80-slot runtime table. Do not implement the SDK bridge or change IRQ counts
until this layout is proven.

## Corrections to earlier conclusions

The following earlier conclusions are explicitly retracted:

- The stable root cause is **not** “320 MHz changes the bootloader watchdog from about 8 seconds to about 1 second.” Current board behavior showed an approximately 8-second expiry matching the configured timeout.
- Raw APB/AON register writes are **not** the final WDT solution. They were removed; the stable path uses the SDK wrapper and NuttX automonitor.
- `bk_aon_wdt_stop()` is not the current failure source; it operates successfully in the board-verified chain.
- `bk_timer_start: ret=-0x1f02` identifies an uninitialized SDK timer driver but was not the final cause of the 8-second reset.

## Current code and diagnostic state

- `board/bk7258_t5ai/chip/bk7258_vectors.c`
  - SysTick slot 15 and external IRQ slots 16 through 63 use `exception_common`.
  - the external-IRQ entry change and UART RX are `board-verified`.
  - the unused temporary `bk7258_systick_probe()` remains and must be removed during cleanup.
- `board/bk7258_t5ai/chip/bk7258_wdt.c`
  - temporary `A/B/K` markers remain;
  - the temporary one-tick `W/T` probe remains;
  - controlled SDK initialization and TIMER_ID2 stop remain.
- `[ipc_svr] create_socket failed.` is a separate issue and was not part of the WDT reset chain.

## Next single-step sequence

Perform these one at a time, documenting each result here before continuing:

1. Reconcile the bootloader magic at vector slot 64 with the SDK's 64 external IRQs; determine and prove the full runtime VTOR/table layout.
2. Only after that, define the complete NuttX IRQ space (normally 80 logical IRQs for 64 external sources) and board-test the UART/WDT baseline.
3. Implement the CPU0 overlay SDK IRQ bridge without modifying SDK source, then exercise at least one SDK-owned interrupt source.
4. Extend or constrain multicore routing according to the proven AMP/SMP execution model; do not assume all cores share one NuttX IRQ table.
5. Remove `A/B/W/T/K`, the unused SysTick probe, and stale comments after IRQ behavior is stable, then regress WDT, UART RX, DVFS and LittleFS.

## Constraints

- Make contest changes only inside the team overlay.
- Do not modify the SDK source tree or the generated official NuttX/vendor/package checkouts.
- Do not restore raw-register WDT shutdown in NuttX bring-up.
- Build, flash, commit, and push remain separately authorized actions.

## 2026-07-22 — A0 RAM-vector plumbing

### Objective

Enable standard NuttX RAM-vector relocation for the current BK7258 CPU0
configuration without changing the current IRQ count, the 66-entry flash boot
table, or any SDK/official NuttX source.

### Change

- `board/bk7258_t5ai/chip/Kconfig`: `ARCH_CHIP_BK7258` now selects
  `ARCH_RAMVECTORS`; Cortex-M33 already provides `ARCH_HAVE_RAMVECTORS`.
- `board/bk7258_t5ai/scripts/ld.script`: added a retained `NOLOAD`
  `.ram_vectors` RAM section after `.irq_stack`, aligned to 512 bytes, with
  `_sram_vectors`/`_eram_vectors` boundaries and a 512-byte alignment assertion.
- Reused the existing `CONFIG_ARCH_RAMVECTORS`-guarded
  `arm_ramvec_initialize()` call in `bk7258_irq.c`; that file was not changed.

### Scope and static reasoning

- `NR_IRQS` and `ARMV8M_PERIPHERAL_INTERRUPTS` are unchanged.
- The 66-entry flash `_vectors` table and mandatory magic at slots 64/65 are
  unchanged.
- With the current definitions, standard NuttX RAM-vector sizing remains 64
  entries (`0x100` bytes); flash magic entries 64/65 are outside that runtime
  copy and remain in flash. This is a static expectation pending build evidence.
- No SDK IRQ bridge, vector-table expansion, or runtime magic-slot repair was
  implemented.

### Status and evidence boundary

Status: `blocked` (superseded by artifact gate failure below).

The A0 package build completed successfully (exit 0) on 2026-07-22, but
subsequent artifact inspection revealed that `CONFIG_ARCH_RAMVECTORS` was not
set in the generated `.config`, so standard RAM vectors were not activated.
See the "A0 artifact gate failure" section below. The earlier
`build-verified` label referred only to compilation/link/package success and
is superseded by the artifact failure finding.

### Next single minimal action

Targeted read-only diagnosis of the effective BK7258 Kconfig path and why the
`select ARCH_RAMVECTORS` did not propagate into the generated config. Do not
start A1/SDK bridge or flash.

## 2026-07-22 — A0 authorized build result

### Command and workspace

```text
cd /home/lijian/project/open-vela
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh -j8
```

Workspace root: `/home/lijian/project/open-vela`

### Exit code and log

Exit code: **0** (success).
Full build log: `/tmp/bk7258-a0-build-20260722.log`

### Observed output

```text
nuttx.bin       = 156380 bytes
nuttx_crc.bin   = 166158 bytes
bl_crc.bin      = 69632 bytes (physical 0x0 .. 0x69631)
all-app.bin     = 235790 bytes (= bl_crc.bin + nuttx_crc.bin)
logical_size    = 0x262dc
physical_size   = 0x2890e
app_burn_offset = 0x11000
magic           = 424b373233360000
defconfig.tmp   saved (no configuration change)
```

### Evidence boundary

This result proves compilation, linking, CRC expansion, and packaging completed
without error. No further evidence has been gathered:

- Generated `.config` has not been inspected for `CONFIG_ARCH_RAMVECTORS=y` or
  other A0 gate symbols.
- ELF/map section and symbol checks have not been performed (`.ram_vectors`
  placement, 512-byte alignment, `g_ram_vectors` size, `arm_ramvec_initialize`/
  `arm_ramvec_attach` linkage).
- Raw image offsets have not been inspected (flash `_vectors` table, magic at
  slot 64/65, boot header layout).
- No flash, board test, commit, or push was performed.

### Status

`blocked` — package-build success (exit 0) confirmed, but superseded by the
artifact gate failure documented below. `CONFIG_ARCH_RAMVECTORS` was not present
in the generated `.config`, so standard RAM vectors were not activated. A0 is
not build-verified as a functional RAM-vector change and is not `board-verified`.

### Next single minimal action

Targeted read-only diagnosis of the effective BK7258 Kconfig path and why the
`select ARCH_RAMVECTORS` did not propagate into the generated config.

## 2026-07-22 — A0 artifact gate failure: ARCH_RAMVECTORS not enabled

### Evidence report

Full artifact verification report: `/tmp/bk7258-a0-artifact-verification-20260722.md`

### PASS evidence

| Gate | Detail |
|---|---|
| Config | `CONFIG_ARCH_HAVE_CUSTOM_VECTORS=y` present in generated `.config` |
| Flash vectors | `_vectors` at `0x02010000`, symbol size `0x108` (264 bytes, 66 entries) |
| Magic bytes | Exact raw bytes `42 4b 37 32 33 36 00 00` (`BK7236\0\0`) at `nuttx.bin+0x100` |
| RAM section | `.ram_vectors` output section exists exactly once at a 512-byte-aligned RAM address (`0x28000800`) |
| Partition | `nuttx.bin` = 156,380 bytes (`0x262DC`) < `0x100000` partition gate |
| Packaging | `app_burn_offset=0x11000`, combined magic offset `0x11110`, all-app.bin = bl_crc.bin + nuttx_crc.bin = 235,790 bytes |

### FAIL evidence

| Gate | Detail |
|---|---|
| Config | `CONFIG_ARCH_RAMVECTORS` absent / not set in generated `.config` |
| RAM section size | `.ram_vectors` section size is `0x0` (expected `0x100`) |
| Symbol span | `_sram_vectors == _eram_vectors == 0x28000800` (end minus start = `0x0`) |
| g_ram_vectors | Symbol absent from ELF and map |
| arm_ramvec_initialize | Not linked into ELF (0 definitions) |
| arm_ramvec_attach | Not linked into ELF (0 definitions) |

### Conclusion

Build/package success did not activate standard RAM vectors. The immediate
evidence boundary is that the source-level `select ARCH_RAMVECTORS` did not
propagate into the generated config used by this build. Kconfig
reachability/effective-path diagnosis has not been performed; the root cause
is not yet claimed.

### Status

`blocked`. No flash, board test, commit, or push was performed.

### Next single minimal action

Targeted read-only diagnosis of the effective BK7258 Kconfig path/symbol
dependency and why the select is absent. Do not start A1/SDK bridge or flash.

## 2026-07-22 — A0 artifact failure root cause: stale generated config

### Objective

Prove why `CONFIG_ARCH_RAMVECTORS` was absent from the generated `.config`
despite the source-level `select ARCH_RAMVECTORS` in `ARCH_CHIP_BK7258`, and
establish the root cause before any fix or rebuild.

### Evidence table

| Item | Result | Implication |
|---|---|---|
| Effective vendor `chip/Kconfig` inode | 2487570, md5 `68df40f443ba9cf8f91f217d673b873e` | Same file object |
| Effective team overlay `chip/Kconfig` inode | 2487570, md5 `68df40f443ba9cf8f91f217d673b873e` | Identical content; H1 (path/staleness) rejected |
| Generated `.config` `CONFIG_ARCH_CHIP_BK7258` | `=y` | Correct chip selected |
| Generated `.config` `CONFIG_ARCH_CORTEXM33` | `=y` | Correct architecture |
| Generated `.config` `CONFIG_ARCH_HAVE_CUSTOM_VECTORS` | `=y` | Custom vector prerequisite met |
| Generated `.config` `CONFIG_ARCH_HAVE_RAMVECTORS` | `=y` | RAM-vector capability reported by arch |
| Generated `.config` `CONFIG_ARCH_RAMVECTORS` | `# CONFIG_ARCH_RAMVECTORS is not set` | **Not enabled** |
| H2 dependency reachability | `ARCH_HAVE_RAMVECTORS=y` present; no missing prerequisite | H2 rejected |
| Kconfig `chip/Kconfig` mtime | 2026-07-22 00:24:37 | Select was present before build |
| `.config` mtime | 2026-07-21 18:43:22 | **5 hours 41 minutes 15 seconds older** than Kconfig |
| `configure.sh` invocation | `configure.sh -e` | `-e` = existing config mode |
| Source defconfig vs `nuttx/defconfig` backup | Identical | No defconfig delta to trigger regeneration |
| `configure.sh` behavior | Printed `No configuration change.`; exited before regenerating `.config` or re-evaluating Kconfig | Stale `.config` reused as-is |
| H4 conflicting rule | No Kconfig rule forces `ARCH_RAMVECTORS` off | H4 rejected |

### Hypothesis verdicts

| Hypothesis | Verdict | Reason |
|---|---|---|
| H1 — wrong/stale Kconfig file path | **Rejected** | Vendor and overlay `chip/Kconfig` resolve to identical inode 2487570 and md5 `68df40f443ba9cf8f91f217d673b873e` |
| H2 — missing Kconfig dependency | **Rejected** | `CONFIG_ARCH_HAVE_RAMVECTORS=y` is present; `select` does not require the user to enable anything |
| H3 — stale generated `.config` not regenerated after Kconfig edit | **Confirmed** | Kconfig mtime (00:24:37) is 5h41m15s newer than `.config` mtime (18:43:22); `configure.sh -e` saw identical defconfig and exited early |
| H4 — conflicting rule forces RAMVECTORS off | **Rejected** | No such rule found in Kconfig tree |

### Root-cause conclusion

The Kconfig `select ARCH_RAMVECTORS` edit in `chip/Kconfig` (mtime 2026-07-22
00:24:37) was never evaluated during the A0 build. The build ran
`configure.sh -e`, which compares the source defconfig against the backup
`nuttx/defconfig`. Because they were identical, `configure.sh` printed
`No configuration change.` and exited early, reusing the stale generated
`.config` (mtime 2026-07-21 18:43:22 — 5 hours 41 minutes 15 seconds older
than the Kconfig edit). The `select` was therefore never evaluated, and
`CONFIG_ARCH_RAMVECTORS` remained unset. This is H3: stale generated config
reused after Kconfig source edit.

### Evidence boundary

- The Kconfig `select` source edit is proven present and correct.
- The stale `.config` reuse is proven by mtime delta and `configure.sh -e`
  early-exit behavior.
- No conflicting Kconfig rule was found (H4 rejected).
- No fix has been applied. No rebuild, flash, board test, commit, or push
  occurred after this diagnosis.

### Status

`blocked` — root cause proven; artifact failure is a config-staleness issue,
not a Kconfig or source defect. A0 remains not build-verified as a functional
RAM-vector change and not `board-verified`.

### Candidate next actions (requiring explicit user authorization/decision)

Two candidate config-refresh approaches exist:

1. **Persist `CONFIG_ARCH_RAMVECTORS=y` in the team overlay defconfig** so that
   `configure.sh -e` detects a defconfig change and regenerates `.config`,
   causing Kconfig `select` to be re-evaluated.
2. **Force a clean config regeneration path** (e.g., `distclean` + `configure.sh`
   without `-e`, or manual deletion of `.config`) before rebuilding.

Neither approach has been selected or executed. The smallest next action is
obtaining explicit user authorization for one config-refresh approach. Do not
proceed to A1/SDK bridge, flash, or board test until the config-refresh
choice is authorized and a rebuild confirms `CONFIG_ARCH_RAMVECTORS=y` in
the generated `.config`.

## 2026-07-22 — A0 config-refresh path authorized

### User decision

User selected approach 2: **distclean + rebuild**.

The approved Kconfig `select ARCH_RAMVECTORS` in `ARCH_CHIP_BK7258` remains
the sole source-level enablement. `CONFIG_ARCH_RAMVECTORS=y` will NOT be
added to the team overlay defconfig. The test is whether a freshly generated
config (from distclean) correctly evaluates the existing `select` statement.

### Authorized commands

Only these two commands are authorized for the next action:

```text
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh distclean
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh -j8
```

### Scope boundary

Flash, board testing, source/config/defconfig edits, commit, and push remain
unauthorized. No artifact inspection until after the fresh rebuild completes
and the result is recorded here.

### Status

`blocked` — pending the fresh rebuild and artifact gate check.

### Next single minimal action

Run the documented distclean, then rebuild once, and record the result (exit
code, `.config` symbols, `.ram_vectors` section, `g_ram_vectors` symbol,
`arm_ramvec_initialize` linkage) here before any artifact inspection or
further action.

## 2026-07-22 — A0 authorized distclean and fresh rebuild result

### Commands and workspace

```text
cd /home/lijian/project/open-vela
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh distclean
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh -j8
```

Workspace root: `/home/lijian/project/open-vela`

### Distclean

- Command: `./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh distclean`
- Exit code: **0** (success)
- Log: `/tmp/bk7258-a0-distclean-20260722.log`
- Observed final lines:

```text
No configuration change.
make: Entering directory '/home/lijian/project/open-vela/nuttx'
make: Leaving directory '/home/lijian/project/open-vela/nuttx'
```

The `No configuration change.` line is recorded factually. Whether the A0
`select ARCH_RAMVECTORS` was activated during distclean has not been
determined from this output alone; the artifact gate (fresh `.config`
inspection) is the authoritative check.

### Fresh rebuild

- Command: `./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh -j8`
- Exit code: **0** (success)
- Log: `/tmp/bk7258-a0-clean-rebuild-20260722.log`

### Generated output

```text
nuttx.bin       = 156436 bytes (logical 0x26314, physical 0x28952)
nuttx_crc.bin   = 166226 bytes
bl_crc.bin      = 69632 bytes (physical 0x0 .. 0x69631)
all-app.bin     = 235558 bytes (= bl_crc.bin + nuttx_crc.bin)
```

### Edit and action boundary

No source, config, defconfig, or docs files were edited by the build runner.
No flash, board test, commit, or push was performed.

### Evidence boundary

Only command success and package output are known from this result. The fresh
`.config`, `.ram_vectors` section sizing, `g_ram_vectors` symbol,
`arm_ramvec_initialize`/`arm_ramvec_attach` linkage, ELF/map layout, and raw
image magic/offsets have **not** yet been inspected.

### Status

`blocked` — distclean and rebuild both succeeded (exit 0), but the artifact
gate (fresh `.config` containing `CONFIG_ARCH_RAMVECTORS=y`, `.ram_vectors`
section size `0x100`, `g_ram_vectors` present, `arm_ramvec_initialize` linked)
has not been checked. A0 remains not build-verified as a functional
RAM-vector change and not `board-verified`.

### Next single minimal action

Inspect the fresh generated config/ELF/map/raw image once to determine
whether `CONFIG_ARCH_RAMVECTORS=y` is now present and RAM vectors are
active. Do not flash.

## 2026-07-22 — A0 fresh artifact gate: 15/16 pass, arm_ramvec_attach discarded

### Evidence report

Full artifact verification report:
`/tmp/bk7258-a0-clean-artifact-verification-20260722.md`

### PASS evidence (15 gates)

| Gate | Detail |
|---|---|
| Config | `CONFIG_ARCH_HAVE_CUSTOM_VECTORS=y`, `CONFIG_ARCH_HAVE_RAMVECTORS=y`, `CONFIG_ARCH_RAMVECTORS=y` — all three present |
| RAM section | `.ram_vectors` exactly once, type `SHT_NOBITS` (NOLOAD), address `0x28000800`, size `0x100`, 512-byte aligned |
| Symbol span | `_sram_vectors=0x28000800`, `g_ram_vectors` size `0x100` (OBJECT, BSS), `_eram_vectors=0x28000900`; end minus start = `0x100` |
| arm_ramvec_initialize | Exactly one definition at `0x020107f0`, 48 bytes |
| Flash vectors | `_vectors` at `0x02010000`, symbol size `0x108` (264 bytes, 66 entries) |
| Magic bytes | Exact raw bytes `42 4b 37 32 33 36 00 00` (`BK7236\0\0`) at `nuttx.bin+0x100` |
| Partition | `nuttx.bin` = 156,436 bytes (`0x26314`) < `0x100000` partition gate |
| Packaging | `app_burn_offset=0x11000`, combined magic offset `0x11110`, all-app.bin = bl_crc.bin + nuttx_crc.bin = 235,858 bytes |

### FAIL evidence (1 gate)

| Gate | Detail |
|---|---|
| arm_ramvec_attach | `arm_m/arm_ramvec_attach.c` compiled per build log (`CC: arm_m/arm_ramvec_attach.c`) but `arm_ramvec_attach` has zero defined symbols in final ELF and is absent from map file. Linker garbage collection removed it because the current binary has no retained call site referencing this function. |

### Conclusion

The stale `.config` root cause from the earlier artifact failure is **resolved**:
`CONFIG_ARCH_RAMVECTORS=y` is now set, `.ram_vectors` is a proper `SHT_NOBITS`
section of `0x100` bytes at `0x28000800`, and `arm_ramvec_initialize` is linked.
The RAM vector table and bulk initialization path are in place.

The approved A0 gate is 15/16 and remains `blocked` on understanding and
retaining the standard per-vector attach path. `arm_ramvec_attach` was compiled
but the linker discarded it because no code in the current binary calls it.
This means the runtime `irq_attach` -> `arm_ramvec_attach` path is unavailable.

### Evidence boundary

- Stale `.config` issue: **resolved** (clean rebuild produced correct config).
- RAM table and initialization path: **linked** (`arm_ramvec_initialize` present).
- Per-vector attach path: **not retained** (`arm_ramvec_attach` absent from ELF).
- No flash, VTOR, board test, commit, or push was performed.

### Status

`blocked` — 15/16 gates pass; single remaining failure is `arm_ramvec_attach`
absent from final ELF due to linker dead-code elimination.

### Next single minimal action

Targeted read-only trace of NuttX `irq_attach` / RAM-vector integration and
current config to determine why no final reference retains `arm_ramvec_attach`.
Do not fix, rebuild, flash, or start A1/SDK bridge yet.

## 2026-07-22 — A0 remaining gate root cause: arm_ramvec_attach is A1-only

### Objective

Explain why `arm_ramvec_attach` is absent from the final ELF despite
`CONFIG_ARCH_RAMVECTORS=y` being correctly set, and classify the failure as
an implementation defect, a build/link defect, or a verification-plan defect.

### Call-path evidence

| Item | Source file | Role |
|---|---|---|
| `arm_ramvec_initialize()` | `nuttx/arch/arm/src/arm_m/arm_ramvec.c` | Bulk-copies the flash vector table into `g_ram_vectors[]`; called once during early init. |
| `arm_ramvec_attach()` | `nuttx/arch/arm/src/arm_m/arm_ramvec_attach.c` | Writes a single `g_ram_vectors[]` slot at runtime; called by chip/board code that installs a direct RAM vector for a specific IRQ. |
| `irq_attach()` | `nuttx/sched/irq/irq_attach.c` | Writes the software `g_irqvector[]` dispatch table only; **never** calls `arm_ramvec_attach()`. |

All actual NuttX callers of `arm_ramvec_attach()` in the upstream tree are
explicit chip/board low-latency or direct-vector users — for example, Nordic
SDC and STM32 high-priority timer/vector examples. These are opt-in callers
that deliberately install a RAM-resident fast-path vector for a specific IRQ.

The current BK7258 A0 port calls only `arm_ramvec_initialize()`. It does not
install any direct RAM vectors; all IRQs route through
`exception_common -> arm_doirq -> irq_dispatch -> g_irqvector[]`. There are
zero call sites for `arm_ramvec_attach()` in the current binary.

### Hypothesis verdicts

| Hypothesis | Verdict | Reason |
|---|---|---|
| H1 — generic NuttX integration should have retained `arm_ramvec_attach` | **Rejected** | `irq_attach()` writes only `g_irqvector[]` and never calls `arm_ramvec_attach()`. The two are independent dispatch mechanisms. |
| H2 — linker GC correctly discards an unreferenced function | **Confirmed** | `arm_ramvec_attach.c` is compiled (build log shows `CC: arm_m/arm_ramvec_attach.c`) but no code in the final binary references the symbol. Linker `--gc-sections` correctly removes it. |
| H3 — build or link defect caused the symbol to be lost | **Rejected** | Compilation succeeded; the source is present. The only reason it is not in the final ELF is the absence of any call site, which is correct behavior for the current A0 configuration. |

### Defect classification

This is a **verification-plan defect**, not an implementation or build defect.

The plan (`/home/lijian/.claude/plans/happy-snuggling-dawn.md`) A0 line 49
requires `arm_ramvec_attach` to be present in the final ELF as an artifact
gate. However, the same plan introduces the first call site for
`arm_ramvec_attach` only in A1 runtime repair (lines 109–118). A0 has no
caller, so linker GC correctly discards the function. The gate is
overconstrained: it requires a symbol that the plan itself does not wire in
until the next substage.

### Evidence boundary

- `arm_ramvec_initialize` (bulk init): **linked** at `0x020107f0`, 48 bytes.
- `arm_ramvec_attach` (per-vector repair): **compiled**, zero references in final binary, correctly discarded by `--gc-sections`.
- `irq_attach` path: writes `g_irqvector[]` only; proven independent of `arm_ramvec_attach`.
- All NuttX upstream callers are explicit chip/board low-latency users; none are generic.
- Artificial `EXTERN`/`KEEP`/dummy retention would add unused code and satisfy only the symbol-count gate, with no runtime correctness effect.

### Status

`blocked` — pending user decision on the conflicting gate. No source, plan,
config, defconfig, build, flash, board test, commit, or push has been
performed after this diagnosis.

### Decision options

1. **Recommended:** Move the `arm_ramvec_attach` final-symbol requirement from A0 to A1, where the plan introduces its first call site (runtime repair lines 109–118). This aligns the artifact gate with the actual call-graph boundary and requires no dead-symbol retention.
2. **Fallback:** Force `arm_ramvec_attach` to be retained in the linker script (e.g., `EXTERN` or `KEEP` in `.ram_vectors` support) to satisfy the A0 gate as currently written. This adds unused code with no runtime correctness effect.

Neither option has been selected or executed.

### Next single minimal action

Obtain user decision on which gate-correction approach to apply. Do not edit
source, plan, config, or defconfig; do not rebuild, flash, board-test,
commit, or push until the decision is recorded.

## 2026-07-22 — A0 verification gate corrected; build verification complete

### Objective

Correct the overconstrained A0 verification gate that required `arm_ramvec_attach`
in the final ELF, and confirm A0 is `build-verified` under the corrected gate.

### Decision

User selected the recommended move-to-A1 decision (option 1 from the prior entry):
move the `arm_ramvec_attach` final-symbol requirement from A0 to A1, where the plan
introduces its first call site (runtime repair of slots 64/65). No `EXTERN`, `KEEP`,
dummy calls, or source/linker workaround will be added.

### Root cause

The verification plan's A0 gate required both `arm_ramvec_initialize()` and
`arm_ramvec_attach()` to link exactly once. However, `arm_ramvec_attach()` has no call
site in A0 — its first explicit caller appears only in A1 runtime repair (plan lines
109–118). Linker `--gc-sections` correctly discards the unreferenced function. This is a
**plan defect**, not an implementation or build defect.

### Corrected A0 gate

The corrected A0 verification gate requires only:

- `CONFIG_ARCH_HAVE_CUSTOM_VECTORS=y` and `CONFIG_ARCH_RAMVECTORS=y`;
- `.ram_vectors` is `SHT_NOBITS`/NOLOAD, `0x100` bytes, at `0x28000800`, 512-byte aligned;
- `_sram_vectors`/`g_ram_vectors`/`_eram_vectors` span agrees (`0x100`);
- `arm_ramvec_initialize()` links exactly once;
- flash `_vectors` at `0x02010000`, size `0x108` (66 entries);
- raw magic `42 4b 37 32 33 36 00 00` at image offset `0x100`;
- image below `0x100000` partition gate.

`arm_ramvec_attach()` is NOT required in A0. Its presence becomes an A1 gate after the
explicit slot 64/65 runtime repair calls are added.

### No rebuild needed

The gate correction is a plan-only change. No source, config, defconfig, linker script,
or SDK file was modified. The clean rebuild from the prior entry (distclean + rebuild,
exit 0, `all-app.bin` = 235,558 bytes) remains the authoritative build. No additional
rebuild was performed.

### Corrected A0 artifact evidence (from clean rebuild)

| Gate | Detail |
|---|---|
| Config | `CONFIG_ARCH_HAVE_CUSTOM_VECTORS=y`, `CONFIG_ARCH_HAVE_RAMVECTORS=y`, `CONFIG_ARCH_RAMVECTORS=y` |
| RAM section | `.ram_vectors` exactly once, type `SHT_NOBITS` (NOLOAD), address `0x28000800`, size `0x100`, 512-byte aligned |
| Symbol span | `_sram_vectors=0x28000800`, `g_ram_vectors` size `0x100` (OBJECT, BSS), `_eram_vectors=0x28000900` |
| arm_ramvec_initialize | Exactly one definition at `0x020107f0`, 48 bytes |
| arm_ramvec_attach | Absent from ELF — expected in A0; deferred to A1 gate |
| Flash vectors | `_vectors` at `0x02010000`, symbol size `0x108` (264 bytes, 66 entries) |
| Magic bytes | Exact raw bytes `42 4b 37 32 33 36 00 00` (`BK7236\0\0`) at `nuttx.bin+0x100` |
| Partition | `nuttx.bin` = 156,436 bytes (`0x26314`) < `0x100000` partition gate |
| Packaging | `app_burn_offset=0x11000`, combined magic offset `0x11110`, all-app.bin = 235,558 bytes |

### Status

`build-verified`. Not `board-verified`. No flash, board test, commit, or push was
performed.

### Next single minimal action

Obtain separate flash authorization, then verify VTOR points to `g_ram_vectors`,
slots 15/31 contain `exception_common`, UART/NSH work, `T/K` present, no WDT reset,
and LittleFS reads `BK7258LFS-OK`. Do not start A1 or SDK bridge before board
verification.

## 2026-07-22 — A0 board observation: boot baseline passes; VTOR/slot proof pending

### J-Link session

```text
J-Link> mem32 0x28000800 4
0x28000800 = 0x2809FFFC 0x02010129 0x02010119 0x02010119
```

J-Link connected via SWD, identified STAR r1p0. J-Link warned that the
configured Cortex-M33 does not match the identified STAR, and warned that old
J-Link firmware may not handle I/D cache correctly. These are informational
warnings from the J-Link software; no functional impact was observed from the
warnings alone.

### Serial boot evidence

```text
bootloader entered
app partition 0x02010000
jump 0x02010000
N4Clk tier 5 / M1 0x420 / hz 0x1312d000
ABWT
NuttShell (NSH)
K
```

UART/NSH accepted `cat /data/probe.txt` and produced output `BK7258LFS-OK`.

### Debugger-connection-correlated reset observation

The target reboots when J-Link establishes the connection. Without that
connection-induced event the shown firmware boots normally. This is recorded as
a debugger-connection-correlated reset observation, not yet as a WDT or firmware
regression root cause. J-Link connect warning/reset may perturb the board and
needs a minimal follow-up after connection; do not conclude firmware
spontaneously resets.

### PASS / board-observed evidence

| Item | Status | Detail |
|---|---|---|
| Bootloader accepts image | PASS | bootloader entered, app partition `0x02010000`, jump `0x02010000` |
| Reaches NSH | PASS | `ABWT` markers, `NuttShell (NSH)` prompt, `K` automonitor marker |
| UART RX / command path | PASS | `cat /data/probe.txt` accepted and executed |
| LittleFS persistent file | PASS | output `BK7258LFS-OK` |
| WDT automonitor markers | PASS | `ABWT` then `K` observed; no spontaneous 8-second reset during the shown boot |
| RAM-vector base plausible | PASS | `mem32 0x28000800 4` reads `0x2809FFFC 0x02010129 0x02010119 0x02010119`; values are plausible copied initial vector entries, not zero-fill or `0xAA` SRAM fill |

### NOT yet proven / pending evidence

| Item | Status | Detail |
|---|---|---|
| VTOR points to RAM | NOT PROVEN | VTOR register `0xE000ED08` was not read; the command read `mem32 0x28000800 4`, not the VTOR register itself |
| Slot 15 (SysTick) value | NOT PROVEN | RAM address `0x2800083C` (slot 15) was not read |
| Slot 31 value | NOT PROVEN | RAM address `0x2800087C` (slot 31) was not read |

### Status and evidence boundary

A0 remains `build-verified` with partial board evidence. The boot baseline
(bootloader jump, NSH prompt, UART RX, WDT markers, LittleFS read, plausible
RAM-vector contents) all pass. However, the three critical RAM-vector proof
points — VTOR register value, slot 15 content, and slot 31 content — have not
been read. Therefore A0 is NOT yet `board-verified`.

The debugger-connection-correlated reset observation is noted but does not
constitute proof of a firmware spontaneous reset. The board boots normally
without J-Link connection; the reset occurs when J-Link establishes SWD.

### Next single minimal action

While J-Link is already connected and the board has returned to NSH, read VTOR
(`0xE000ED08`) plus RAM slots 15 (`0x2800083C`) and 31 (`0x2800087C`). Do not
flash or enter A1.

### Static artifact evidence from clean-rebuild ELF/raw image

The following static values were extracted from the current `build-verified`
clean-rebuild ELF and `nuttx.bin`. These are build-artifact evidence only;
actual runtime values remain pending J-Link reads while the board is at NSH.

| Item | Static artifact value |
|---|---|
| `exception_common` (ELF symbol) | `0x0201084c` |
| Thumb-bit stored vector pointer | `0x0201084d` (bit 0 set for Thumb) |
| `nuttx.bin` raw slot 15 at offset `0x3c` | `0x0201084d` |
| `nuttx.bin` raw slot 31 at offset `0x7c` | `0x0201084d` |

The hardware vector pointers in the raw binary carry the Thumb bit (bit 0 set),
as required by the Cortex-M33 vector table specification. Both slot 15 (SysTick)
and slot 31 in the flash image contain the same `exception_common` entry.

### Expected runtime values (pending J-Link reads)

While J-Link is already connected and the board has returned to NSH, the
following reads are expected to confirm runtime RAM-vector relocation:

| J-Link command | Expected result | Meaning |
|---|---|---|
| `mem32 0xE000ED08 1` | `0x28000800` | VTOR points to RAM vector base |
| `mem32 0x2800083C 1` | `0x0201084D` | RAM slot 15 (SysTick) contains `exception_common` with Thumb bit |
| `mem32 0x2800087C 1` | `0x0201084D` | RAM slot 31 contains `exception_common` with Thumb bit |

These expected values are derived from static artifact analysis. If the actual
runtime reads match, A0 RAM-vector relocation can be promoted to
`board-verified`. If they differ, the discrepancy must be diagnosed before
proceeding.

Status: remains `build-verified` with partial board evidence; not
`board-verified`.

## 2026-07-22 — A0 RAM-vector relocation board verification complete

### Objective

Record the final J-Link VTOR and RAM-slot reads that complete A0 RAM-vector
relocation board verification, combining them with the immediately preceding
boot-baseline evidence.

### J-Link runtime reads (board at NSH, J-Link connected)

```text
J-Link> mem32 0xE000ED08 1
E000ED08 = 28000800

J-Link> mem32 0x2800083C 1
2800083C = 0201084D

J-Link> mem32 0x2800087C 1
2800087C = 0201084D
```

### Runtime-to-artifact match

| Item | Runtime read | Static artifact expectation | Match |
|---|---|---|---|
| VTOR (`0xE000ED08`) | `0x28000800` | `g_ram_vectors` at `0x28000800` | PASS |
| Slot 15 (`0x2800083C`) | `0x0201084D` | `exception_common` (`0x0201084c`) with Thumb bit set | PASS |
| Slot 31 (`0x2800087C`) | `0x0201084D` | `exception_common` (`0x0201084c`) with Thumb bit set | PASS |

All three runtime reads exactly match the clean-rebuild ELF and raw-image
expectations. The Cortex-M33 VTOR points to the RAM vector base at
`0x28000800`, and RAM slots 15 (SysTick) and 31 contain the correct
`exception_common|1` Thumb entry.

### Combined board evidence (preceding boot baseline + J-Link reads)

| Item | Status | Detail |
|---|---|---|
| Bootloader accepts image | PASS | bootloader entered, app partition `0x02010000`, jump `0x02010000` |
| Reaches NSH | PASS | `ABWT` markers, `NuttShell (NSH)` prompt, `K` automonitor marker |
| UART RX / command path | PASS | `cat /data/probe.txt` accepted and executed |
| LittleFS persistent file | PASS | output `BK7258LFS-OK` |
| WDT automonitor markers | PASS | `ABWT` then `K` observed; no spontaneous 8-second reset during the shown run |
| VTOR points to RAM | PASS | `0xE000ED08` reads `0x28000800` |
| Slot 15 (SysTick) | PASS | `0x2800083C` reads `0x0201084D` (`exception_common|1`) |
| Slot 31 | PASS | `0x2800087C` reads `0x0201084D` (`exception_common|1`) |

### Full A0 gate table (artifact + board)

| Gate | Detail | Source |
|---|---|---|
| Config | `CONFIG_ARCH_HAVE_CUSTOM_VECTORS=y`, `CONFIG_ARCH_HAVE_RAMVECTORS=y`, `CONFIG_ARCH_RAMVECTORS=y` | artifact |
| RAM section | `.ram_vectors` exactly once, type `SHT_NOBITS` (NOLOAD), address `0x28000800`, size `0x100`, 512-byte aligned | artifact |
| Symbol span | `_sram_vectors=0x28000800`, `g_ram_vectors` size `0x100`, `_eram_vectors=0x28000900` | artifact |
| arm_ramvec_initialize | Exactly one definition at `0x020107f0`, 48 bytes | artifact |
| arm_ramvec_attach | Absent from ELF — expected in A0; deferred to A1 gate | artifact |
| Flash vectors | `_vectors` at `0x02010000`, symbol size `0x108` (66 entries) | artifact |
| Magic bytes | Exact raw bytes `42 4b 37 32 33 36 00 00` at `nuttx.bin+0x100` | artifact |
| Partition | `nuttx.bin` = 156,436 bytes (`0x26314`) < `0x100000` | artifact |
| Packaging | `app_burn_offset=0x11000`, combined magic offset `0x11110`, `all-app.bin` = 235,558 bytes | artifact |
| Bootloader acceptance | app partition `0x02010000`, jump succeeds | board |
| NSH boot | `ABWT` + `K` markers, `NuttShell (NSH)` prompt | board |
| UART RX / NSH command | `cat /data/probe.txt` accepted, output `BK7258LFS-OK` | board |
| LittleFS persistence | probe file read confirms `BK7258LFS-OK` | board |
| WDT automonitor | `ABWT` then `K`; no firmware-spontaneous reset in shown run | board |
| VTOR → RAM | `0xE000ED08` = `0x28000800` | board |
| RAM slot 15 | `0x2800083C` = `0x0201084D` | board |
| RAM slot 31 | `0x2800087C` = `0x0201084D` | board |

### Tooling caveat: J-Link connection-correlated reset

J-Link V9 establishes SWD as STAR r1p0. The J-Link software reports that the
configured Cortex-M33 does not match the identified STAR and warns that old
J-Link firmware may not handle I/D cache correctly. Additionally, the target
reboots when J-Link establishes the SWD connection. This is recorded as a
**debugger-connection-correlated reset / tooling caveat**, not as an A0 or WDT
regression. After reconnection and reboot, the runtime reads shown above are
correct and match expectations. The warnings are informational J-Link software
messages; no functional impact was observed from the warnings alone.

### Status

`board-verified`. A0 RAM-vector relocation is complete: VTOR points to RAM,
RAM vector slots contain the correct `exception_common` entries, boot baseline
passes (bootloader jump, NSH, UART RX, WDT automonitor, LittleFS), and no
firmware-spontaneous reset is observed in the shown run.

### Action boundary

- No agent-performed flash, download, commit, or push occurred.
- All flashing/download remains user-only.
- The next allowed technical action is A1 adaptation from the governing plan
  (explicit slot 64/65 runtime repair calls, `arm_ramvec_attach` A1 gate).
- SDK IRQ bridge is not started and remains gated behind A1.
- `A/B/W/T/K` diagnostic markers and the unused SysTick probe remain pending
  final cleanup after IRQ behavior is stable.

## 2026-07-22 — A1 RED compile/link invariant gates installed

### Objective

Install test-first (TDD RED) compile-time and link-time invariants that express
the expected A1 IRQ facts, without changing the current production A0 IRQ
definitions, 66-entry flash vector table, or runtime initialization behavior.

### Invariants added

#### `board/bk7258_t5ai/chip/include/irq.h` — compile-time gates

| Invariant | Expected A1 value | Current A0 value | Gate mechanism |
|---|---|---|---|
| `NR_IRQS` | 80 | 48 | `_Static_assert(NR_IRQS == 80)` |
| `ARM_VECTAB_SIZE` | 80 | 64 (=16+48) | `_Static_assert(ARM_VECTAB_SIZE == 80)` |
| External IRQ count | 64 | 48 | Checked via NR_IRQS assertion |
| Runtime vector alignment | 512 | 512 | Linker ASSERT (existing) |
| ETHERNET anchor | NuttX IRQ 64 | undefined | `_Static_assert(BK7258_IRQ_ETHERNET == 64)` |
| SCALE0 anchor | NuttX IRQ 65 | undefined | `_Static_assert(BK7258_IRQ_SCALE0 == 65)` |
| MAILBOX anchor | NuttX IRQ 79 | undefined | `_Static_assert(BK7258_IRQ_MAILBOX == 79)` |
| SMP rejection | CPU0-only | CPU0-only | `#error` if `CONFIG_SMP` set |
| Boot magic offset 0 | 0x100 | 0x100 | `_Static_assert(BK7258_MAGIC_BOOT0_OFFSET == 0x100)` |
| Boot magic offset 1 | 0x104 | 0x104 | `_Static_assert(BK7258_MAGIC_BOOT1_OFFSET == 0x104)` |

New macros defined: `BK7258_IRQ_ETHERNET`, `BK7258_IRQ_SCALE0`,
`BK7258_IRQ_MAILBOX`, `BK7258_A1_EXTERNAL_IRQ_COUNT`, `BK7258_A1_NR_IRQS`,
`ARM_VECTAB_SIZE` (guarded), `BK7258_MAGIC_BOOT0_OFFSET`,
`BK7258_MAGIC_BOOT1_OFFSET`, `BK7258_MAGIC_BOOT_SIZE`.

#### `board/bk7258_t5ai/scripts/ld.script` — linker ASSERT gates

| Invariant | Expected A1 value | Current A0 value | Gate mechanism |
|---|---|---|---|
| `.vectors` section size | 0x140 | 0x108 | `ASSERT(. - ORIGIN(FLASH) == 0x140)` |
| Flash `_vectors` start | 0x02010000 | 0x02010000 | `. = ORIGIN(FLASH)` (unchanged) |
| `.ram_vectors` size | 0x140 | 0x100 | `ASSERT(_eram_vectors - _sram_vectors == 0x140)` |
| `.ram_vectors` alignment | 512 | 512 | `ASSERT((_sram_vectors & 0x1ff) == 0)` (unchanged) |
| Magic offset plausibility | >0x100 | 0x108 | `ASSERT(. - ORIGIN(FLASH) > 0x100)` |

### Scope and behavior boundary

- Production `NR_IRQS` (48), `ARMV8M_PERIPHERAL_INTERRUPTS`, vector array
  size/contents, external IRQ definitions, `up_irqinitialize()` sequence, and
  runtime repair calls are **unchanged**.
- The 66-entry flash boot table and mandatory magic at slots 64/65 are
  **unchanged**.
- No SDK IRQ bridge, vector-table expansion, or runtime magic-slot repair was
  implemented.
- No dummy calls, `EXTERN`, dead-code retention, or production workarounds were
  added.

### Status

`static-only`. RED gates are installed. The build is expected to fail on the
newly added A1 invariants (primarily `NR_IRQS == 80` and `.vectors == 0x140`).

### Next single minimal action

Run the authorized build to confirm the RED gates fail as expected.

## 2026-07-22 — A1 RED build result: gates catch missing A1 behavior

### Command and workspace

```text
cd /home/lijian/project/open-vela
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh -j8
```

Workspace root: `/home/lijian/project/open-vela`

### Exit code and log

Exit code: **2** (build failure).
Full build log: `/tmp/bk7258-a1-red-build-20260722.log`

### Decisive failure lines

```text
/home/lijian/project/open-vela/nuttx/include/arch/chip/irq.h:112:1: error: static assertion failed: "A1 gate: NR_IRQS must be 80 (16+64 external)"
/home/lijian/project/open-vela/nuttx/include/arch/chip/irq.h:123:1: error: static assertion failed: "A1 gate: ARM_VECTAB_SIZE must be 80"
```

Both errors are from the newly added A1 compile-time invariants in `irq.h`.
No unrelated syntax, environment, or toolchain errors occurred. The build
failed at the first compilation unit that includes `<arch/chip/irq.h>`,
confirming the `_Static_assert` gates fire before any link-time ASSERT could
be reached.

### Conclusion

The RED gates correctly catch the missing A1 behavior. `NR_IRQS` is currently
48 (A0) but the gate requires 80 (A1). `ARM_VECTAB_SIZE` is currently 64
(=16+48) but the gate requires 80. The invariants are functioning as intended:
the build cannot proceed until the A1 production changes expand the IRQ count
to 80 and the vector table to 80 entries.

### Production A0 behavior preserved

The following A0 production values are unchanged and were NOT implemented or
modified:

- `NR_IRQS` remains 48
- `ARMV8M_PERIPHERAL_INTERRUPTS` remains 48
- 66-entry flash vector table and magic at slots 64/65 unchanged
- `up_irqinitialize()` sequence unchanged
- No runtime repair calls added

### Status

`static-only`. RED gates verified. Build fails on A1 invariants as expected.

### Next single minimal action

Implement A1 production changes: expand `NR_IRQS` to 80, extend the vector
table to 80 entries (0x140 bytes), and add runtime repair of boot-magic slots
64/65 via `arm_ramvec_attach`.

---

## 2026-07-22 -- A1 Task 1 v2 correction and re-test

### Reason for correction

Independent review found 5 issues with the v1 RED gates:
- F1 [Critical]: Missing `ARMV8M_PERIPHERAL_INTERRUPTS == 64` gate (now present)
- F2 [Important]: `#ifndef ARM_VECTAB_SIZE` guard in `irq.h` could silently shadow the gate (removed from irq.h; gate now only in bk7258_vectors.c)
- F3 [Important]: Linker `.vectors` magic offset assertion `>0x100` was imprecise (removed; replaced by explicit `_vectors == ORIGIN(FLASH)` + `SIZEOF(.vectors) == 0x140`)
- F4 [Minor]: "82 entries" documentation error (corrected to "80 entries")
- F5 [Minor]: Hardcoded `48` in magic offset assertion (clarified with comment)
- Controller: production IRQ names `BK7258_IRQ_ETHERNET/SCALE0/MAILBOX` removed from public `irq.h`; all value checks moved to `bk7258_vectors.c` with conditional `_Static_assert(0)` branches

### Scope of v2 changes

| File | Change |
|---|---|
| `irq.h` | Removed A1 invariants section; kept SMP rejection + boot-magic constants only |
| `bk7258_vectors.c` | Replaced gates: removed `BK7258_A1_VECTAB_SIZE`/`_ALIGN` defs; added `ARMV8M_PERIPHERAL_INTERRUPTS==64`, `ARM_VECTAB_SIZE==80` with `#ifdef` guard, `VECTAB_ALIGN==512`; IRQ anchors use conditional `_Static_assert(0)` branches; no production IRQ name definitions |
| `ld.script` | Unchanged (already correct per v2 requirements) |
| `n6-sdk-integration-worklog.md` | Corrected "82 entries" to "80 entries" (2 locations); appended v2 section |
| `next-stage-prompt.md` | Corrected "82 entries" to "80 entries" (1 location) |

### Build command

```bash
cd /home/lijian/project/open-vela
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh -j8
```

### Exit code and log

Exit code: **2** (build failure).
Full build log: `/tmp/bk7258-a1-red-build-20260722-v2.log`

### Decisive failure lines

```text
chip/bk7258_vectors.c:117:3: error: static assertion failed: "A1 gate: BK7258_EXTERNAL_IRQS not defined; expected 64"
chip/bk7258_vectors.c:121:1: error: static assertion failed: "A1 gate: NR_IRQS must be 80 (16 + 64 external)"
chip/bk7258_vectors.c:124:1: error: static assertion failed: "A1 gate: ARMV8M_PERIPHERAL_INTERRUPTS must be 64 (external IRQ count)"
chip/bk7258_vectors.c:129:1: error: static assertion failed: "A1 gate: vector table must be 80 entries (0x140 bytes)"
chip/bk7258_vectors.c:138:3: error: static assertion failed: "A1 gate: ARM_VECTAB_SIZE must be 80"
chip/bk7258_vectors.c:154:3: error: static assertion failed: "A1 gate: BK7258_IRQ_ETHERNET not defined; expected 64"
chip/bk7258_vectors.c:162:3: error: static assertion failed: "A1 gate: BK7258_IRQ_SCALE0 not defined; expected 65"
chip/bk7258_vectors.c:170:3: error: static assertion failed: "A1 gate: BK7258_IRQ_MAILBOX not defined; expected 79"
```

All 8 errors are from the v2 RED gates in `bk7258_vectors.c`. No syntax, environment, or toolchain errors. No errors from `irq.h` (production IRQ names removed). No errors from linker (compilation fails first).

### Conclusion

The v2 RED gates correctly catch all missing A1 behavior. All 8 gates fire from
`bk7258_vectors.c` (not from irq.h or ld.script). The gates express every
required A1 invariant: external count=64, NR_IRQS=80, ARMV8M_PERIPHERAL_INTERRUPTS=64,
ARM_VECTAB_SIZE=80, VECTAB_ALIGN=512, anchor IRQs 64/65/79, magic offsets 0x100/0x104.

### Production A0 behavior preserved (unchanged from v1)

- `NR_IRQS` remains 48
- `ARMV8M_PERIPHERAL_INTERRUPTS` remains 48
- 66-entry flash vector table and magic at slots 64/65 unchanged
- `up_irqinitialize()` sequence unchanged
- No runtime repair calls added

### Status

`static-only`. v2 RED gates verified. Build fails on A1 invariants as expected.

### Next single minimal action

Independent re-review of v2 gates, then implement A1 production changes.

---

## 2026-07-22 -- A1 Task 1 v2 review verdict

### Review result

Independent read-only review of v2 RED gates: **Spec: PASS**, **Quality: APPROVED**.
All 5 prior findings (F1-F5 from v1 review) verified resolved. Three Minor
non-blocking observations noted for Task 2 cleanup only:

| # | Severity | Description | Task 2 action |
|---|---|---|---|
| v2-F1 | Minor | Tautological `_Static_assert(512 == 512)` adds zero gate value (real alignment gate is linker ASSERT at ld.script:152-153) | Remove from bk7258_vectors.c:145-146; do not weaken linker gate |
| v2-F2 | Minor | Comment at bk7258_vectors.c:132-134 inaccurately says `ARM_VECTAB_SIZE` is defined in `arch/arm_m/irq.h` as `(16 + NR_IRQS)`; actual source is `arch/arm/src/arm_m/ram_vectors.h` as `(ARMV8M_PERIPHERAL_INTERRUPTS + NVIC_IRQ_FIRST)` | Fix comment; gate value itself is correct |
| v2-F3 | Minor | Duplicate `_Static_assert(NR_IRQS == 80)` at lines 121 and 129 | Remove duplicate at line 129; keep line 121 |

### Review evidence

- Spec compliance matrix: all 12 brief requirements have correct gates
- Production A0 behavior preserved: NR_IRQS=48, ARMV8M_PERIPHERAL_INTERRUPTS=48, 66-entry table, up_irqinitialize() unchanged, no runtime repair calls
- No future production macros defined by RED tests
- ARM_VECTAB_SIZE semantics verified: 16 + 64 = 80 (not 16 + 80 = 96)
- Linker ASSERTs valid GNU ld syntax, correctly placed after section definitions
- File scope: only approved 7 files touched; chip.h and bk7258_irq.c unchanged from baseline

### Status

`static-only`. Task 1 RED gates accepted. Ready for Task 2 minimal GREEN implementation.

### Next single minimal action

Implement A1 production changes (Task 2 GREEN): expand `NR_IRQS` to 80 (16+64), set `ARMV8M_PERIPHERAL_INTERRUPTS` to 64, extend vector table to 80 entries (0x140 bytes), add runtime repair of boot-magic slots 64/65 via `arm_ramvec_attach`. Clean up three Minor notes during refactor only if doing so does not weaken gates.

## 2026-07-22 -- A1 Task 2 minimal GREEN source implementation

### Objective

Implement the minimum A1 production behavior needed to satisfy the accepted
Task 1 RED gates and the required runtime-vector repair, per
`/tmp/bk7258-a1-task2-brief.md`.

### Source changes

#### `board/bk7258_t5ai/chip/include/irq.h`

| Item | Old (A0) | New (A1) |
|---|---|---|
| `NR_IRQS` | `48` | `(BK7258_IRQ_FIRST + BK7258_EXTERNAL_IRQS)` = 80 |
| `BK7258_IRQ_FIRST` | `(16)` (local) | `16` (public, used by NR_IRQS) |
| `BK7258_EXTERNAL_IRQS` | not defined | `64` |
| `BK7258_IRQ_UART1` | `(16 + 15)` | `(BK7258_IRQ_FIRST + 15)` |
| `BK7258_IRQ_ETHERNET` | not defined | `(BK7258_IRQ_FIRST + 48)` = 64 |
| `BK7258_IRQ_SCALE0` | not defined | `(BK7258_IRQ_FIRST + 49)` = 65 |
| `BK7258_IRQ_MAILBOX` | not defined | `(BK7258_IRQ_FIRST + 63)` = 79 |
| Comments | N1/48-external/66-entry | A1/64-external/80-entry |

SMP rejection and boot-magic constants preserved unchanged.

#### `board/bk7258_t5ai/chip/include/chip.h`

| Item | Old (A0) | New (A1) |
|---|---|---|
| `ARMV8M_PERIPHERAL_INTERRUPTS` | `NR_IRQS` (48) | `BK7258_EXTERNAL_IRQS` (64), guarded with `#ifndef` |

Comment updated from N1 to A1.

#### `board/bk7258_t5ai/chip/chip.h` (internal, symlinked to `nuttx/arch/arm/src/chip/chip.h`)

| Item | Old (A0) | New (A1) |
|---|---|---|
| `ARMV8M_PERIPHERAL_INTERRUPTS` | `NR_IRQS` | `BK7258_EXTERNAL_IRQS` |

This file is included by `arm_internal.h` after the public `chip/include/chip.h`.
Both now define the same value (64); the public one uses `#ifndef` guard to
prevent preprocessor redefinition warning.

#### `board/bk7258_t5ai/chip/bk7258_vectors.c`

| Item | Old (A0) | New (A1) |
|---|---|---|
| Vector table size | `_vectors[66]` | `_vectors[80]` |
| Slots [66..79] | not present | `exception_common` |
| `_Static_assert(512 == 512)` | tautological | `_Static_assert(VECTAB_ALIGN == 512, ...)` with `#ifdef` guard |
| Duplicate `NR_IRQS==80` | lines 121 and 129 | removed duplicate at old line 129 |
| ARM_VECTAB_SIZE comment | "defined by arch/arm_m/irq.h as (16 + NR_IRQS)" | "defined by arch/arm/src/arm_m/ram_vectors.h as (ARMV8M_PERIPHERAL_INTERRUPTS + NVIC_IRQ_FIRST)" |

All RED gates retained. Diagnostic handlers and markers preserved.

#### `board/bk7258_t5ai/chip/bk7258_irq.c`

| Item | Old (A0) | New (A1) |
|---|---|---|
| Include `<arch/barriers.h>` | absent | present |
| Disable loop bound | `NR_IRQS - BK7258_IRQ_FIRST` | `BK7258_EXTERNAL_IRQS` |
| After VTOR write | no barrier | `UP_DSB(); UP_ISB();` |
| RAM vec init | `arm_ramvec_initialize()` only | same, plus slot 64/65 repair |
| Slot repair | not present | `arm_ramvec_attach(ETHERNET/SCALE0, exception_common)` |
| Error handling | not present | `PANIC()` on failure |
| Post-repair barrier | not present | `UP_DSB(); UP_ISB();` |
| Debug assertions | not present | `getreg32(NVIC_VECTAB)==g_ram_vectors`, slots 64/65 |

All later priority/handler/stack/unmask logic unchanged.

### Binding production definitions implemented

```c
#define BK7258_IRQ_FIRST                16
#define BK7258_EXTERNAL_IRQS            64
#define NR_IRQS                         (BK7258_IRQ_FIRST + BK7258_EXTERNAL_IRQS)  /* = 80 */
#define BK7258_IRQ_UART1                (BK7258_IRQ_FIRST + 15) /* logical 31 */
#define BK7258_IRQ_ETHERNET             (BK7258_IRQ_FIRST + 48) /* logical 64 */
#define BK7258_IRQ_SCALE0               (BK7258_IRQ_FIRST + 49) /* logical 65 */
#define BK7258_IRQ_MAILBOX              (BK7258_IRQ_FIRST + 63) /* logical 79 */
#define ARMV8M_PERIPHERAL_INTERRUPTS    BK7258_EXTERNAL_IRQS    /* = 64 */
```

Resulting common ARM relationships:
- `BK7258_EXTERNAL_IRQS` = 64
- `NR_IRQS` = 16 + 64 = 80
- `ARMV8M_PERIPHERAL_INTERRUPTS` = 64
- `ARM_VECTAB_SIZE` = 16 + 64 = 80
- `VECTAB_ALIGN` = 512

### Binding flash vector layout implemented

```text
[0]       initial MSP
[1]       __start
[2..3]    bk7258_hardfault_handler (diagnostic)
[4..63]   exception_common
[64]      BK7258_APP_MAGIC_WORD0 (raw offset 0x100)
[65]      BK7258_APP_MAGIC_WORD1 (raw offset 0x104)
[66..79]  exception_common
```

80 entries, 0x140 bytes. Slots 64/65 are inside the table.

### Binding `up_irqinitialize()` ordering implemented

1. Disable all 64 external NVIC lines (loop bound `BK7258_EXTERNAL_IRQS`).
2. Point VTOR at flash `_vectors`.
3. `UP_DSB(); UP_ISB();` after VTOR write.
4. `arm_ramvec_initialize()` copies all 80 entries and switches VTOR to `g_ram_vectors`.
5. `arm_ramvec_attach(BK7258_IRQ_ETHERNET, exception_common)` and
   `arm_ramvec_attach(BK7258_IRQ_SCALE0, exception_common)`.
6. Both return values checked; failure calls `PANIC()`.
7. `UP_DSB(); UP_ISB();` after the two RAM slot writes.
8. Debug assertions: `getreg32(NVIC_VECTAB) == g_ram_vectors`,
   `g_ram_vectors[64] == (exception_common | 1)`,
   `g_ram_vectors[65] == (exception_common | 1)`.
9. Continue default priorities, system-handler attachments, IRQ stack
   coloring, and `up_irq_enable()`.

No interrupt unmasked between flash VTOR write and completed slot 64/65 repair.

### Three Minor review notes resolved

| # | Action | Gate impact |
|---|---|---|
| v2-F1 | Replaced `_Static_assert(512 == 512)` with `_Static_assert(VECTAB_ALIGN == 512, ...)` under `#ifdef VECTAB_ALIGN` | No weakening; genuine gate |
| v2-F2 | Fixed comment: ARM_VECTAB_SIZE source is `ram_vectors.h` as `(ARMV8M_PERIPHERAL_INTERRUPTS + NVIC_IRQ_FIRST)`, not `arch/arm_m/irq.h` | Comment-only |
| v2-F3 | Removed duplicate `_Static_assert(NR_IRQS == 80)` (kept the one at the core IRQ count gates section) | No weakening; one assertion retained |

### Additional change: `board/bk7258_t5ai/chip/chip.h` (internal)

The arch-level chip.h (`nuttx/arch/arm/src/chip/chip.h`, symlink to
`board/bk7258_t5ai/chip/chip.h`) also defined `ARMV8M_PERIPHERAL_INTERRUPTS`
as `NR_IRQS`. Since `arm_internal.h` includes this file after the public
`chip/include/chip.h`, its definition would override ours, producing
`ARM_VECTAB_SIZE` = 80+16 = 96 instead of the required 80. Changed to
`BK7258_EXTERNAL_IRQS` (64). Added `#ifndef` guard in the public chip.h to
prevent preprocessor redefinition warning.

### Status

`build-verified`. Source changes complete. GREEN build passed (exit 0). No
flash, board test, commit, or push performed. Board status remains as A0
`board-verified`; A1 is NOT board-verified.

### Next single minimal action

Task 2 independent review. Then Task 3 artifact inspection.

---

## 2026-07-22 -- A1 Task 2 GREEN build result

### Command and workspace

```text
cd /home/lijian/project/open-vela
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh distclean
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh -j8
```

Workspace root: `/home/lijian/project/open-vela`

### Exit code and log

Exit code: **0** (success).
Full build log: `/tmp/bk7258-a1-green-build-20260722.log`

### Observed output

```text
nuttx.bin       = 156604 bytes
nuttx_crc.bin   = 166396 bytes
bl_crc.bin      = 69632 bytes (physical 0x0 .. 0x69631)
all-app.bin     = 236028 bytes (= bl_crc.bin + nuttx_crc.bin)
```

### Compilation warnings

Zero `ARMV8M_PERIPHERAL_INTERRUPTS` redefinition warnings (the `#ifndef`
guard in the public chip.h prevents them). Zero `error:` lines. The only
warnings are from SDK headers (`CONFIG_SOC_BK7257` not defined) which are
pre-existing and unrelated.

### Evidence boundary

This result proves compilation, linking, CRC expansion, and packaging completed
without error. All accepted RED gates (static assertions and linker ASSERTs)
passed. No artifact inspection (ELF symbols, `.ram_vectors` size, config
symbols) has been performed yet.

### Status

`build-verified`. Not `board-verified`. No flash, board test, commit, or push
was performed.

### Next single minimal action

Task 2 independent review. Then Task 3 artifact inspection.

---

## 2026-07-22 -- A1 Task 2 review findings and fixes

### Independent review verdict

- **Spec: PASS**
- **Quality: APPROVED**

### Findings

| # | Severity | File | Fix |
|---|---|---|---|
| F1 [Important] | Quality | `bk7258_irq.c:200-201` | Cast LHS to `(uintptr_t)` and use anchor indices `BK7258_IRQ_ETHERNET`/`BK7258_IRQ_SCALE0` |
| F2 [Important] | Scope | `chip/chip.h` (internal) | Accepted by controller; technically required by dual include paths; team-overlay owned |
| F3 [Minor] | Quality | `chip/include/chip.h` | Add `#elif ARMV8M_PERIPHERAL_INTERRUPTS != BK7258_EXTERNAL_IRQS #error` |
| F4 [Minor] | Cosmetic | `bk7258_vectors.c:6` | "Stage N2" -> "Stage A1" |

### Controller scope adjudication

The extra `board/bk7258_t5ai/chip/chip.h` change is accepted and added to
Task-2 scope because it is technically required by the dual include paths, is
team-overlay-owned, and the human's higher-level authorization is overlay-only
A1 source changes. The original task brief's file list was incomplete; do not
revert this required macro fix.

### Exact fixes applied

1. `bk7258_irq.c`: `(uintptr_t)g_ram_vectors[BK7258_IRQ_ETHERNET] == ((uintptr_t)exception_common | 1u)` and same for `BK7258_IRQ_SCALE0`. Eliminates pointer/integer comparison warning.
2. `chip/include/chip.h`: `#ifndef` define plus `#elif ARMV8M_PERIPHERAL_INTERRUPTS != BK7258_EXTERNAL_IRQS #error`. Does not silently accept wrong prior value.
3. Comments corrected:
   - `bk7258_vectors.c` header: "Stage N2" -> "Stage A1".
   - `bk7258_vectors.c` assertions: "RED gates" -> "permanent A1 compile-time invariants"; removed false "current A0 definitions differ" text.
   - `irq.h`: slots 64/65 correspond to external IRQ indices 48/49 but logical NuttX IRQs 64/65.
   - `ld.script`: header "Stage N1" -> "Stage A1"; "66 entries, 0x108 bytes" -> "80 entries, 0x140 bytes"; gate comments describe current A1 state.
   - `bk7258_irq.c` header: accurately notes diagnostic slots [2..3] and runtime-repaired [64]/[65].

### Status

`build-verified` (pending rebuild to confirm F1 warnings gone). Not
`board-verified`. No flash, commit, or push.

### Next single minimal action

Rebuild to confirm F1 pointer/integer warnings are eliminated.

---

## 2026-07-22 -- A1 Task 2 review-fix rebuild result

### Command and workspace

```text
cd /home/lijian/project/open-vela
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh distclean
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh -j8
```

Workspace root: `/home/lijian/project/open-vela`

### Exit code and log

Exit code: **0** (success).
Full build log: `/tmp/bk7258-a1-green-build-20260722-v2.log`

### Observed output

```text
nuttx.bin       = 156604 bytes
nuttx_crc.bin   = 166396 bytes
bl_crc.bin      = 69632 bytes (physical 0x0 .. 0x69631)
all-app.bin     = 236028 bytes (= bl_crc.bin + nuttx_crc.bin)
```

### Warning scan

- `comparison between pointer and integer`: **0** (F1 confirmed fixed)
- `ARMV8M_PERIPHERAL_INTERRUPTS.*redefined`: **0** (F3 confirmed fixed)
- `error:`: **0**
- All remaining warnings are from SDK headers (`CONFIG_SOC_BK7257`, etc.) -- pre-existing, unrelated.

### Evidence boundary

Build exit 0 with zero team-file errors and zero F1 pointer/integer warnings.
No artifact inspection, flash, board test, commit, or push performed.

### Status

`build-verified`. Not `board-verified`. No flash, commit, or push.

### Next single minimal action

Task 2 re-review. Then Task 3 artifact inspection.

---

## 2026-07-22 -- A1 Task 2 re-review acceptance

Task 2 re-review complete: **Spec PASS**, **Quality APPROVED**, **0 remaining findings**. All F1-F4 resolved. v2 build exit 0 with sizes unchanged (`nuttx.bin`=156604 B, `all-app.bin`=236028 B) and zero team-file warnings.

A1 is `build-verified` / `static-only`, NOT `board-verified`. No flash, no commit, no bridge.

### Next single minimal action

Task 3 static artifact inspection.

---

## 2026-07-22 -- A1 Task 3 static artifact verification (RETRACTED -- see adversarial review below)

Initial verifier run produced a false 23/23 PASS claim. Adversarial review found the verifier hardcodes symbol addresses, has unconditional always-PASS gates (G09/G12/G19/G21/G23), incomplete source-boundary checks (G22 only checks nuttx/), and a critical postbuild script hex/decimal formatting bug producing fabricated offsets `0x69888`/`0x69631`. The unqualified 23/23 acceptance is retracted. Status is `blocked` pending corrected verifier RED run and postbuild fix.

### Adversarial review

- **File**: `/tmp/bk7258-a1-task3-review.md`
- **Spec: FAIL** (postbuild script emits decimal-as-hex physical offsets)
- **Quality: CHANGES_REQUIRED** (4 Important findings: hardcoded values, vacuous G16, incomplete G22, unconditional gates)

### Findings requiring correction

| # | Severity | Finding |
|---|----------|---------|
| F1 | Critical | `postbuild.sh` prints `0x$((BL_SIZE - 1))` = `0x69631` (decimal disguised as hex); actual hex `0x10FFF`. Same bug on magic offset: `0x$((BL_SIZE + 0x100))` = `0x69888`; correct CRC-expanded value is `0x11110`. |
| F2 | Critical | Verifier hardcodes `EXCEPTION_COMMON`, `START_ADDR`, `HARDFAULT_ADDR`, `RAM_VECTORS_ADDR` instead of deriving from current ELF. |
| F3 | Critical | G22 only checks `nuttx/` for official-tree diffs; ignores `apps/`, `frameworks/`, `packages/`, `external/`, `vendor/`. |
| F4 | Important | G15 hardcodes `up_irqinitialize` function address range. |
| F5 | Important | G16 `irq_attach` operand extraction can silently fail (vacuous pass). |
| F6 | Important | G09/G12/G19/G21/G23 are unconditional always-PASS stubs. |
| F7 | Minor | G16 label conflates SVCall and HardFault (both labeled "SVCall/HardFault"). |

### Pre-fix snapshots

- postbuild.sh hash: `4fe752745f3ebbfdd980673cc8355a9476dd5f99e05a06de83126bae992da799`
- Overlay manifest: `/tmp/bk7258-a1-task3-prefix-manifest.txt`
- Docs hashes: `/tmp/bk7258-a1-task3-prefix-docs-hash.txt`

### Next single minimal action

Correct verifier (dynamic derivation, real predicates, expanded source checks) then run RED against v2 artifacts. Expect 22/23 (postbuild offset gate fails). Then fix postbuild.sh, rebuild v3, run GREEN.

---

## 2026-07-22 -- A1 Task 3 RED run (corrected verifier, v2 artifacts)

Corrected verifier (all symbols derived dynamically from ELF, no hardcoded addresses, real predicates on all 23 gates, expanded source checks, postbuild log cross-check) run against v2 build artifacts and v2 build log.

### Command

```
python3 /tmp/bk7258-a1-verify-artifacts.py 2>&1 | tee /tmp/bk7258-a1-task3-verify-v2-red.log
```

### Result: 22/23 PASS, 1 FAIL

- **G20 FAIL**: Postbuild log physical offsets are decimal-masquerading-as-hex.
  - `bl_crc.bin` end: log says `0x69631` (decimal 69631 with `0x` prefix), actual hex = `0x10FFF`
  - Magic offset: log says `0x69888` (decimal 69888 with `0x` prefix), actual file offset = `0x11110`
  - Decisive mismatch confirmed by dynamic byte search in all-app.bin.

All other 22 gates PASS with genuine predicates (dynamic symbol derivation, cross-checks, operand extraction, source boundary against all official trees).

### Next action

Fix `postbuild.sh` (decimal-as-hex formatting + CRC-expanded offset formula), rebuild v3, rerun verifier.

---

## 2026-07-22 -- A1 Task 3 postbuild.sh fix

Fixed `postbuild.sh` (team overlay only, no official-tree changes):

1. **Line 87**: `0x$((BL_SIZE - 1))` (decimal-as-hex) replaced with `$(printf '%x' $((BL_SIZE - 1)))` (true hex). For current build: `0x69631` (wrong) becomes `0x10fff` (correct).

2. **Line 90**: `0x$((BL_SIZE + 0x100))` (decimal-as-hex + wrong offset) replaced with `$(printf '0x%x' $((BL_SIZE + 0x110)))` (true hex + CRC-expanded offset). For current build: `0x69888` (wrong) becomes `0x11110` (correct).

3. **Header comment (line 15)**: Updated magic offset claim from `0x11100` to `0x11110` with CRC expansion explanation.

No changes to packaging bytes/concatenation behavior. Build artifacts (bl_crc.bin, nuttx_crc.bin, all-app.bin) remain byte-identical.

### Next action

Rebuild v3, then run corrected verifier against v3 artifacts.

---

## 2026-07-22 -- A1 Task 3 GREEN run (v3 build + verifier)

### v3 build

- Command: `./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh -j8`
- Log: `/tmp/bk7258-a1-green-build-20260722-v3.log`
- Exit: 0
- Postbuild output now correct:
  - `bl_crc.bin = 69632 bytes (physical 0x0 .. 0x10fff)` (was `0x69631`)
  - `app magic in all-app.bin @ 0x11110 (physical)` (was `0x69888`)
- Artifact sizes unchanged: `nuttx.bin`=156604 B, `all-app.bin`=236028 B
- Build artifacts have new SHA-256 hashes vs v2 (NuttX builds embed timestamps; expected). Postbuild packaging semantics/concatenation unchanged. Only v3 hashes are final.

### v3 verifier (GREEN)

- Command: `python3 /tmp/bk7258-a1-verify-artifacts.py 2>&1 | tee /tmp/bk7258-a1-task3-verify-v3.log`
- Exit: 0
- **23/23 PASS. STATUS: ALL GATES PASS.**

All gates now have genuine predicates with dynamic ELF derivation:

| Gate | Result | Key evidence |
|------|--------|-------------|
| G01 | PASS | `CONFIG_ARCH_RAMVECTORS=y` |
| G02 | PASS | `CONFIG_SMP` not enabled, `SMP_NCPUS=1` |
| G03 | PASS | `_vectors` 1 def at `0x02010000` |
| G04 | PASS | `_vectors` size 320 = `0x140` (parsed decimal from readelf) |
| G05 | PASS | `.vectors` PROGBITS, `0x02010000`, `0x140` |
| G06 | PASS | 80-word layout matches dynamically-derived symbols |
| G07 | PASS | `BK7236\0\0` at `0x100` |
| G08 | PASS | Slots 66..79 all `exception_common|1` |
| G09 | PASS | Symbols cross-verified against sections + binary |
| G10 | PASS | `.ram_vectors` NOBITS, RAM, `0x140`, align 512 |
| G11 | PASS | `g_ram_vectors` 1 def, matches section addr/size |
| G12 | PASS | RAM slot 64/65 within `.ram_vectors` bounds |
| G13 | PASS | `arm_ramvec_initialize` 1 def |
| G14 | PASS | `arm_ramvec_attach` 1 def |
| G15 | PASS | 1 + 2 calls, bounds from ELF symbol |
| G16 | PASS | IRQ operands fully extracted: {3, 11}, labeled SVCall/HardFault |
| G17 | PASS | 156604 B < `0x100000` |
| G18 | PASS | End `0x020363bc` < window `0x02110000` |
| G19 | PASS | Nonzero sizes, SHA-256, all-app identity verified |
| G20 | PASS | Postbuild log `0x10fff`/`0x11110` MATCH actual offsets |
| G21 | PASS | Final firmware nonzero, identity verified |
| G22 | PASS | 5 git-based official trees clean; vendor repo-managed (not independently verified) |
| G23 | PASS | Source delta = postbuild.sh + 2 docs only |

### Final firmware

- **Path**: `/home/lijian/project/open-vela/nuttx/all-app.bin`
- **Size**: 236028 bytes (`0x399fc`)
- **SHA-256**: `33f68aa7261ad4e7ae163c2322fef6f64f96c7d0eeb76295816c20238ebc2293`

### Expected VTOR/RAM addresses for J-Link validation

- VTOR: `0x28000800` (g_ram_vectors)
- RAM slot 64 (`0x28000900`): expect `0x020108fd` (exception_common | 1)
- RAM slot 65 (`0x28000904`): expect `0x020108fd` (exception_common | 1)

### Status

`build-verified` / `static-only`. NOT `board-verified`. No flash, commit, or push.

### Concerns

None from initial F1-F8. Re-review identified 4 residual findings (R1-R4); all closed in subsequent fix pass.

### Next single minimal action

Close R1-R4 (fail-closed G20, report correction, G22/G23 wording), positive+negative tests, then final Task-3 re-review.

---

## 2026-07-22 -- A1 Task 3 re-review residual findings (R1-R4)

Re-review (`/tmp/bk7258-a1-task3-review.md` lines 275-374): **Spec: PASS**, **Quality: CHANGES_REQUIRED**.

| # | Severity | Finding | Status |
|---|----------|---------|--------|
| R1 | Important | G20 WARNING paths do not set `g20_ok = False` (fail-open) | Pending fix |
| R2 | Important | Report "byte-identical to v2" factually wrong (SHA-256 values differ) | Worklog corrected; report pending |
| R3 | Minor | G22 matrix overclaims vendor cleanliness | Worklog corrected; verifier pending |
| R4 | Minor | G23 manifest blind spot for post-snapshot new tracked files | Pending fix |

### R1-R4 closure evidence

**R1 (G20 fail-closed)**: Fixed. Both `else` branches in G20 now set `g20_ok = False` and print `FAIL:` instead of `WARNING:`. Negative test confirms: with postbuild offset lines removed from log, G20 FAILs and verifier exits nonzero.

**R2 (report "byte-identical")**: Fixed. Report and worklog now state: "v3 rebuild produced new artifact hashes (NuttX builds embed timestamps; expected). Only v3 hashes are final."

**R3 (G22 vendor overclaim)**: Fixed. Gate description now reads "5 git-based official trees verified clean; vendor repo-managed (not independently verified)". Vendor status explicitly labeled unverified.

**R4 (G23 tracked-file blind spot)**: Fixed. G23 now compares current `git ls-files` path set against manifest paths; new tracked files after snapshot are flagged unless in allowed list.

### Positive test (v4)

- Command: `python3 /tmp/bk7258-a1-verify-artifacts.py 2>&1 | tee /tmp/bk7258-a1-task3-verify-v4.log`
- Exit: 0
- Result: 23/23 PASS
- Final firmware hash unchanged: `33f68aa7261ad4e7ae163c2322fef6f64f96c7d0eeb76295816c20238ebc2293`

### Negative test (missing postbuild offsets)

- Created `/tmp/bk7258-a1-green-build-20260722-v3-missing-offsets.log` (v3 log with both physical-offset lines removed; mtime preserved)
- Command: `BK7258_A1_BUILD_LOG=/tmp/bk7258-a1-green-build-20260722-v3-missing-offsets.log python3 /tmp/bk7258-a1-verify-artifacts.py 2>&1 | tee /tmp/bk7258-a1-task3-verify-missing-offsets-negative.log`
- Exit: 1 (via `PIPESTATUS[0]`; `tee` itself exits 0)
- Result: 22/23 PASS, G20 FAIL: "FAIL: bl_crc physical end not found in build log" + "FAIL: magic offset not found in build log"
- Confirms fail-closed behavior.

### All R1-R4 closed. Status

`build-verified` / `static-only`. NOT `board-verified`. No flash, commit, or push.

### Next single minimal action

Final Task-3 re-review.

---

## 2026-07-22 -- A1 Task 3 final re-review acceptance

Final re-review (`/tmp/bk7258-a1-task3-review.md`): **Spec: PASS**, **Quality: APPROVED**, **0 findings**.

All prior findings (F1-F8 initial, R1-R4 residual) fully resolved. No remaining defects.

### Evidence summary

| Item | Value |
|------|-------|
| Positive test (v4) | 23/23 PASS, exit 0 |
| Negative test (missing offsets) | 22/23, G20 FAIL, exit 1 (fail-closed confirmed) |
| Final firmware path | `/home/lijian/project/open-vela/nuttx/all-app.bin` |
| Final firmware size | 236028 bytes (0x399fc) |
| Final firmware SHA-256 | `33f68aa7261ad4e7ae163c2322fef6f64f96c7d0eeb76295816c20238ebc2293` |
| v3 build log | `/tmp/bk7258-a1-green-build-20260722-v3.log` |
| Verifier script | `/tmp/bk7258-a1-verify-artifacts.py` |
| v4 verifier log | `/tmp/bk7258-a1-task3-verify-v4.log` |
| Negative test log | `/tmp/bk7258-a1-task3-verify-missing-offsets-negative.log` |
| Task 3 report | `/tmp/bk7258-a1-task3-report.md` |

### Status

`build-verified` / `static-only`. NOT `board-verified`. No flash, commit, or push.

### Next single minimal action

Final read-only A1 review + user-only flashing/J-Link handoff.

---

## 2026-07-22 -- A1 Task 4 final whole-A1 review and user board handoff

### Final review result

Full review: `/tmp/bk7258-a1-final-review.md`

| Verdict | Value |
|---------|-------|
| Ready for user board verification | **YES** |
| Spec | **PASS** |
| Quality | **APPROVED** |
| Critical findings | 0 |
| Important findings | 0 |
| Minor findings | 3 (all deferred) |

### Three deferred Minor findings

| # | Description | Deferral reason |
|---|-------------|-----------------|
| 1 | A few comments conflate external IRQ indices with logical NuttX/vector-slot numbers (e.g. `irq.h:17-20` says slots 64/65 occupy "logical IRQ numbers 48 and 49"; they are external indices 48/49 but logical NuttX IRQs/vector slots 64/65) | Comment-only; production macros, repair calls, disassembly operands, and ELF/binary layout use correct logical values |
| 2 | Magic-slot compile-time arithmetic checks are not mechanically tied to actual array designators (`bk7258_vectors.c:180-186` vs `:316-320`) | Future-regression hardening issue only; linker gates the real vector base/size; current ELF/binary independently prove magic is in slots 64/65 |
| 3 | Verifier source-boundary gates are not universally fail-closed (`git diff` return codes ignored; untracked files not included) | Does not invalidate current result; independent review found zero tracked official-tree changes; vendor/nested repo coverage caveat already disclosed by G22 |

All three deliberately deferred so the exact reviewed artifact is not changed before board testing.

### Corrected handoff evidence

Full corrected procedure: `/tmp/bk7258-a1-board-handoff-evidence.md`

### Immutable firmware identity (do not rebuild before testing)

| Item | Value |
|------|-------|
| Path | `/home/lijian/project/open-vela/nuttx/all-app.bin` |
| Size | 236028 bytes (`0x399fc`) |
| SHA-256 | `33f68aa7261ad4e7ae163c2322fef6f64f96c7d0eeb76295816c20238ebc2293` |
| Composition | 69632-byte `bl_crc.bin` + 166396-byte `nuttx_crc.bin` |

### User-only BKFIL command template

Derived for full image from the documented `@offset-length` template:

```bat
bk_loader.exe download -p <COM> -b 6000000 --uart-type OTHER --mainBin-multi <path-to>\all-app.bin@0x0-0x399fc --reboot 1 --fast-link 1
```

### ROM mode entry

Hold BOOT button while powering on or pressing RESET.

### Post-flash UART expectations

- Port: UART1, 460800 8N1
- Expected boot sequence: `jump 0x02010000`, `ABWT`, `NuttShell (NSH)`, `K`
- Expected NSH output: `cat /data/probe.txt` -> `BK7258LFS-OK`
- Stability: no spontaneous reset for 30+ seconds

### J-Link validation (after board returns to NSH)

J-Link connection may trigger a tooling-correlated reboot. Wait until board returns to NSH, then:

| Command | Expected value |
|---------|---------------|
| `mem32 0xE000ED08 1` | `0x28000800` |
| `mem32 0x02010100 2` | `0x32374b42 0x00003633` |
| `mem32 0x2800083c 1` | `0x020108fd` |
| `mem32 0x2800087c 1` | `0x020108fd` |
| `mem32 0x28000900 1` | `0x020108fd` |
| `mem32 0x28000904 1` | `0x020108fd` |

- `r` then `h` is bootloader recovery only and must not be used as the state for runtime-vector validation.
- If any value mismatches, stop and diagnose. Do not start SDK IRQ bridge.

### Action boundary

- No agent-performed flash, download, commit, or push occurred.
- Next single minimal action is user flashing and reporting UART/J-Link evidence.
- SDK IRQ bridge remains blocked until A1 is board-verified.
- A1 remains `build-verified` / `static-only`, NOT `board-verified`.

### Final fresh verification after documentation sync

After the Task 4 worklog and next-stage-prompt documentation edits were applied, a fresh verifier run and artifact identity recheck were performed to confirm no artifact drift.

#### Fresh verifier run

```text
python3 /tmp/bk7258-a1-verify-artifacts.py
```

Log: `/tmp/bk7258-a1-task4-final-verify.log`
Exit: **0**
Result: **23/23 PASS**

#### Artifact identity recheck (unchanged)

| Artifact | Size | SHA-256 |
|----------|------|---------|
| ELF `nuttx` | 1432524 B (`0x15dbcc`) | `c2b2f33d7405606850f2e1b7a47b2dacf4548f1fa06f2876742234a2ef3e7278` |
| `nuttx.bin` | 156604 B (`0x263bc`) | `fca3f29963f99f79a8421528dfb835fab687f972e838343e336534c66bac8641` |
| `nuttx_crc.bin` | 166396 B (`0x289fc`) | `bfddbfa07dc9c385516e65738effcd8e0fec3a951865e8402644b914b6277728` |
| `all-app.bin` | 236028 B (`0x399fc`) | `33f68aa7261ad4e7ae163c2322fef6f64f96c7d0eeb76295816c20238ebc2293` |

All four artifacts are byte-identical to the Task 3 v3 build. No rebuild occurred.

#### Tracked-status result

`repo status` and tracked-only `repo forall` exited nonzero because this manifest has pre-existing missing/skipped nested projects and broad untracked noise (apps/testing/drivers/nist-sts, .claude local files, etc.). Among checked projects, tracked changes were listed only in `contest2026_135_yongwangzhiqian`, consistent with overlay-only work.

This does NOT claim universal whole-workspace cleanliness. The reviewer's vendor/skipped-project coverage caveat (Minor finding 3 from the final review) is retained: vendor is repo-managed and not independently verified; skipped nested projects remain outside the checked scope.

#### Status and next action

Unchanged. A1 remains `build-verified` / `static-only`, NOT `board-verified`. No flash, commit, or push. Next single minimal action is user-only BKFIL flashing and UART/J-Link A1 board verification.

## 2026-07-22 -- A1 80-slot RAM-vector board verification complete

### Objective

Record the user-provided UART/boot and J-Link board evidence for the A1
80-slot RAM-vector build (board-flashed `all-app.bin` 236028 B, SHA-256
`a92352eeea5ebbab4eb4a7a95fd97a73d950816a3620242be8e8fc141030b5a5`) and promote A1 to `board-verified`.

### UART/boot evidence (user-provided)

```text
u_bootloader enter
partition app @ 0x02010000
jump to:0x02010000
JMP
N4Clk tier=00000005 M1=00000420(cs=00000002 cd=00000000) A5=8407876c A9=7c7dc8a4(VDDD=00000007 VDDIG=0000000d) hz=1312d000
ABWT
NuttShell (NSH)
nsh> [ipc_svr]
create_socket failed.
nsh> K
nsh> cat /data/probe.txt
BK7258LFS-OK
```

Notes on the log:

- The real output concatenated `BK7258LFS-OK` with the next `nsh>` prompt
  because the probe file has no trailing newline; content still matched.
- `[ipc_svr] create_socket failed.` is a known non-blocking observation
  already recorded in this worklog (lines ~147-150, ~274-277) and explicitly
  classified near line 410 as a separate issue, not part of the WDT/IRQ reset
  chain. It is not a new A1 regression.
- N4Clk tier 5 with `M1=0x420` (`cs=0x02 cd=0x00`) confirms the 320 MHz
  DVFS baseline is active. `VDDD=0x00000007 VDDIG=0x0000000d` matches the
  board-verified DVFS tier-5 voltage settings.

### J-Link connection evidence (user-provided)

J-Link auto-connect selected `CORTEX-M33`, found STAR r1p0, and emitted the
known configured-core mismatch plus old J-Link firmware I/D-cache warning.
No connection-induced reboot was observed in this exact returned log; the
known tooling caveat is retained but not re-confirmed here.

Exact J-Link reads:

```text
E000ED08 = 28000800
02010100 = 32374B42 00003633
2800083C = 020108FD
2800087C = 020108FD
28000900 = 020108FD
28000904 = 020108FD
```

### Gate table

| Gate | Expected | Observed | Result |
|------|----------|----------|--------|
| Bootloader accepts/jumps to `0x02010000` | jump to `0x02010000` | `jump to:0x02010000` | PASS |
| N4 DVFS tier 5 / 320 MHz baseline | tier 5, M1=0x420, VDDD=7, VDDIG=d | `tier=00000005 M1=00000420 ... VDDD=00000007 VDDIG=0000000d` | PASS |
| ABWTK / NSH / UART RX / WDT stability | `ABWT`, `NuttShell (NSH)`, `K`, `cat` accepted | all observed | PASS |
| LittleFS probe `BK7258LFS-OK` | exact string | `BK7258LFS-OK` (concatenated with next prompt due to missing newline) | PASS |
| VTOR -> RAM `0x28000800` | `0xE000ED08` = `0x28000800` | `E000ED08 = 28000800` | PASS |
| Flash slots 64/65 retain magic | `0x02010100` = `32374B42 00003633` (`BK7236\0\0`) | `02010100 = 32374B42 00003633` | PASS |
| RAM slots 15/31 preserve `exception_common\|1` | `0x2800083C` and `0x2800087C` = `0x020108FD` | both `020108FD` | PASS |
| A1 RAM slots 64/65 repaired to exact `0x020108FD` | `0x28000900` and `0x28000904` = `0x020108FD` | both `020108FD` | PASS |
| A1 64-external/80-logical runtime vector adaptation | all gates above | all gates above | **`board-verified`** |

### Status

`board-verified`. A1 80-slot RAM-vector relocation and repair is complete as of
2026-07-22. All required baselines (VTOR, magic, slots 15/31/64/65, UART/NSH,
WDT automonitor, LittleFS, DVFS tier 5) are board-verified.

### Boundary

- A1 is now `board-verified` as of 2026-07-22.
- This verifies A1 table relocation/repair and all required baselines; it does
  NOT implement or exercise the SDK IRQ bridge or a live SDK-owned upper-range
  peripheral IRQ.
- The SDK IRQ bridge prerequisite is now satisfied/unblocked, but bridge work
  is NOT started and requires a new explicit user instruction.
- `A/B/W/T/K` and other diagnostics remain; no cleanup.
- Flashing was user-performed; no agent flash/download/commit/push.
- Next single minimal action: stop and await explicit authorization for Stage B
  CPU0 SDK IRQ bridge; do not begin automatically.

## 2026-07-22 -- A1 board artifact identity correction before commit

### Context

The Task 3/final static review artifact (`all-app.bin` 236028 B, SHA-256
`33f68aa7261ad4e7ae163c2322fef6f64f96c7d0eeb76295816c20238ebc2293`) remains
valid historical static evidence from the v3 build and 23/23 verifier pass.
However, it was **not** the image the user flashed for the returned A1 board
test.

### User-confirmed board-flashed artifact

User confirmed they manually rebuilt at 2026-07-22 16:04 and that this rebuilt
image is the one they flashed for the A1 board evidence. Current identities:

| Artifact | Size | SHA-256 |
|----------|------|---------|
| ELF `nuttx` | 1432524 B | `ad8e8dc6bf4a0645be22dacd6b3454f0b55f769c4cf8c7ca49dad7c9b91bc45e` |
| `nuttx.bin` | 156604 B | `3f6b6e507f08700bc8a0bf8111bbd6185e49fe479cabdce62be87f932df14051` |
| `nuttx_crc.bin` | 166396 B | `a274f5497c76ada82b2a33a9e0a422808535bbac48886722860d4b19c947fa0d` |
| `all-app.bin` | 236028 B (`0x399fc`) | `a92352eeea5ebbab4eb4a7a95fd97a73d950816a3620242be8e8fc141030b5a5` |

### Precommit verifier correctly rejected stale build log

The precommit verifier correctly rejected reuse of the old v3 build log due
to a 7488-second mtime mismatch between the log and the actual artifacts on
disk. This is correct behavior: the verifier should not claim a fresh logged
build when the log does not match the current artifacts. Do not claim the old
v3 log as a fresh logged build for the board-flashed image.

### ELF layout and symbols still match

The current ELF still has:

- `_vectors` = `0x02010000`, size `0x140`
- `.ram_vectors` = `0x28000800`, size `0x140`, align 512
- `g_ram_vectors` = `0x28000800`
- `exception_common` = `0x020108fd`
- `arm_ramvec_initialize` = `0x02010869`
- `arm_ramvec_attach` = `0x02010899`

These match the user J-Link values from the board-verification session. The
board J-Link reads (VTOR, flash magic slots 64/65, RAM slots 15/31/64/65)
all matched expectations. A1 remains `board-verified`.

### Before commit

After the SDK packaging strategy is decided, perform a fresh logged
build/static verification without conflating it with the board-flashed
identity. The board-flashed artifact and the next committed build artifact
may have different hashes (NuttX embeds timestamps); record each identity
separately and do not substitute one for the other.

## 2026-07-22 -- N6 Task 13: SDK local prerequisite packaging

### Policy decision

Vendor SDK remains a local prerequisite. No file from
`board/bk7258_t5ai/bk_idk/armino_as_lib/` is committed to this repository.
The existing bundle is user-authorized local content and remains in place.

### Vendor_beken precedent comparison

The BK7236N `/home/lijian/project/armino/vendor_beken` precedent commits
proprietary blobs in a dedicated vendor-owned repo with `LICENSE-NOTES.md`.
That approach uses a separate vendor repo/manifest project with plain Git
blobs and explicit restricted license notes. It is NOT automatic
redistribution permission for this team repo. A future self-contained
distribution would require explicit Beken redistribution approval and
preferably the same dedicated-vendor-repo pattern.

### Changes made

| File | Change |
|------|--------|
| `.gitignore` | Added `board/bk7258_t5ai/bk_idk/armino_as_lib/` and `*.bak` rules |
| `.gitignore.example` | Same two rules added for documentation parity |
| `board/bk7258_t5ai/bk_idk/README.md` | New: explains bundle provenance, layout, linked library count, setup script usage, vendor_beken comparison |
| `board/bk7258_t5ai/scripts/setup_bk7258_sdk.sh` | New: executable Apache-2.0 shell script with `--check`, `--install`, `--help` modes |
| `board/bk7258_t5ai/scripts/bk7258_sdk_manifest.sha256` | New: SHA-256 manifest for 374 tracked files |

### Manifest counts

| Category | Count |
|----------|-------|
| Total `.a` libraries in `libs/` | 81 |
| Excluded by `BK_EXCLUDE_LIBS` in Make.defs | 50 |
| Linked (included) `.a` libraries | 31 |
| Header files in `include/` | 341 |
| Config files in `config/` | 2 |
| `.obj` files in `libs/` (not linked, not in manifest) | 4 |
| **Total manifest entries** | **374** |

### No SDK binary added/removed/modified

The setup script and manifest are verification-only tooling. No `.a`, `.h`,
`.obj`, or other SDK binary was added to, removed from, or modified in the
existing local bundle.

### Test results

#### RED/negative: missing directory

```text
$ setup_bk7258_sdk.sh --check /tmp/nonexistent_cp_dir
setup_bk7258_sdk.sh: error: directory does not exist: /tmp/nonexistent_cp_dir
exit=1
```

#### RED/negative: empty directory (missing subdirs)

```text
$ setup_bk7258_sdk.sh --check /tmp/empty_cp_dir
  missing: /tmp/empty_cp_dir/include
  missing: /tmp/empty_cp_dir/config
  missing: /tmp/empty_cp_dir/libs
setup_bk7258_sdk.sh: error: directory structure validation failed
exit=1
```

#### GREEN/positive: existing local bundle

```text
$ setup_bk7258_sdk.sh --check
setup_bk7258_sdk.sh: checking SDK bundle at: .../bk_idk/armino_as_lib/cp
setup_bk7258_sdk.sh: validating directory structure ...
setup_bk7258_sdk.sh: directory structure OK
setup_bk7258_sdk.sh: validating checksums ...
setup_bk7258_sdk.sh: all checksums OK
setup_bk7258_sdk.sh: check PASSED
exit=0
```

#### Syntax check

```text
$ bash -n setup_bk7258_sdk.sh
(exit 0, no errors)
```

### Git status evidence

After changes, `git status --short` confirms:

- `armino_as_lib/` no longer appears as untracked (ignored by rule)
- `*.bak` files (`bk7258_flash_mtd.c.bak`, `bk7258_wdt.c.bak`) no longer appear as untracked (ignored by rule)
- `bk_idk/README.md` visible as new untracked file (NOT ignored)
- `setup_bk7258_sdk.sh` visible as new untracked file
- `bk7258_sdk_manifest.sha256` visible as new untracked file
- `docs/superpowers/` remains visible as untracked (NOT ignored, explicitly excluded at staging time)

`git check-ignore -v` confirms correct rule attribution:
- `armino_as_lib/` ignored by `.gitignore:27:board/bk7258_t5ai/bk_idk/armino_as_lib/`
- `*.bak` files ignored by `.gitignore:30:*.bak`
- `README.md` NOT ignored (exit 1 from `git check-ignore`)

### Status

Packaging hygiene complete. No build, flash, commit, or push performed.

### Next single minimal action

Fresh logged precommit build/static verification, then audited commits/push.
SDK IRQ bridge not started.

## 2026-07-22 -- N6 Task 13: verifier-hardening review fixes

### Findings addressed

| # | Severity | Fix |
|---|----------|-----|
| 1 | Critical | `validate_manifest()` was fail-open: `sha256sum -c ... \|\| true` discarded the return code and only counted lines ending `: FAILED`. Missing files produce `FAILED open or read` which the old grep missed. Replaced with fail-closed: direct `sha256sum -c` exit-code check, nonzero triggers die with diagnostics. |
| 2 | Important | `--install` on a fresh clone has no `bk_idk/armino_as_lib/` parent directory. Added `mkdir -p "$(dirname "$dst_dir")"` before temp copy. |
| 3 | Minor | Report concern #3 corrected: "never removes/overwrites existing destination; removes only its own uniquely-named temp path (`.tmp.$$`)". |

### Manifest ordering

Manifest regenerated with deterministic `LC_ALL=C` sort on path column. Verified: `sort -c -k2` passes, 374 entries, 31 linked libs, 0 excluded libs.

### Test results

All 6 tests pass:

| Test | Description | Exit |
|------|-------------|------|
| 1 | RED: missing directory | 1 (PASS) |
| 2 | RED: empty directory (missing subdirs) | 1 (PASS) |
| 3 | RED: structured-empty (dirs exist, files absent) -- proves fail-closed fix | 1 (PASS) |
| 4 | GREEN: existing bundle 374/374 | 0 (PASS) |
| 5 | --install: fresh-clone-like board (parent absent) -- proves mkdir -p fix | 0 (PASS) |
| 6 | --install: refuses overwrite | 1 (PASS) |

Additional: `bash -n` exit 0, `git diff --check` exit 0, manifest ordering `sort -c -k2` OK, 31 linked/0 excluded libs verified.

### Status

Verifier-hardening complete. No build, flash, commit, or push performed. No SDK binary modified.

### Next single minimal action

Fresh logged precommit build/static verification, then audited commits/push.
SDK IRQ bridge not started.

## 2026-07-22 -- N6 Task 14: fresh precommit build/static verification

### Preflight

| Check | Result |
|-------|--------|
| Branch | `bk7258-n6-ramvectors` (confirmed) |
| SDK check (`setup_bk7258_sdk.sh --check`) | 374/374 checksums OK, exit 0 |
| `*.bak` files present on disk | 2 files (`bk7258_flash_mtd.c.bak`, `bk7258_wdt.c.bak`), git-ignored |
| `bk_idk/armino_as_lib/` present on disk | Yes (SDK bundle required for build), git-ignored |
| `docs/superpowers/` present on disk | Yes, explicitly excluded from scope |
| Staged files | 0 (clean index) |
| Pre-build git status | 18 modified tracked + 13 untracked |

### Fresh build

```text
$ cd /home/lijian/project/open-vela
$ ./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh distclean
exit=0

$ ./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh -j8
exit=0
```

Build logs: `/tmp/bk7258-n6-precommit-distclean2.log`, `/tmp/bk7258-n6-precommit-build2.log`.

### Static artifact verification (23/23 PASS)

Verifier: `/tmp/bk7258-n6-precommit-verify.py` (adapted from `/tmp/bk7258-a1-verify-artifacts.py`, 23 gates preserved).

| Gate | Name | Result |
|------|------|--------|
| G01 | CONFIG_ARCH_RAMVECTORS=y | PASS |
| G02 | CPU0-only, SMP not enabled | PASS |
| G03 | _vectors at 0x02010000 | PASS |
| G04 | _vectors size 0x140 | PASS |
| G05 | .vectors PROGBITS, correct addr/size | PASS |
| G06 | 80-word vector layout | PASS |
| G07 | BK7236 magic at 0x100 | PASS |
| G08 | Slots 66..79 == exception_common\|1 | PASS |
| G09 | Symbol cross-verification | PASS |
| G10 | .ram_vectors NOBITS, RAM, 0x140, align 512 | PASS |
| G11 | g_ram_vectors correct | PASS |
| G12 | RAM slot 64/65 within section | PASS |
| G13 | arm_ramvec_initialize: 1 definition | PASS |
| G14 | arm_ramvec_attach: 1 definition | PASS |
| G15 | Call sites: 1 init + 2 attach | PASS |
| G16 | IRQ set {3, 11}; no HW vector 64/65 | PASS |
| G17 | nuttx.bin < 0x100000 | PASS |
| G18 | Flash window fit | PASS |
| G19 | Artifact identities + concatenation | PASS |
| G20 | Magic offset + postbuild log cross-check | PASS |
| G21 | Final firmware identity | PASS |
| G22 | Official trees clean | PASS |
| G23 | Source-scope: intended paths only | PASS |

#### Negative test for G23

Created temporary `_negative_test_marker.tmp` under team repo; verifier correctly returned exit 1 with `FAIL: unexpected modified/new paths beyond intended scope`. Marker cleaned; re-run returned 23/23 PASS, exit 0.

### git diff --check

```text
$ git diff --check
exit=0
```

No whitespace errors.

### Compiler warnings

| Category | Count |
|----------|-------|
| Team-file warnings (bk7258_t5ai source, excluding bk_idk) | 0 |
| SDK header warnings (bk_idk) | 50 (all redefined macros / undefined CONFIG_* from vendor sdkconfig.h) |
| Team-file errors | 0 |

### Artifact identities (precommit build, compile/static-only)

| Artifact | Size | SHA-256 |
|----------|------|---------|
| `nuttx/nuttx` (ELF) | 1432524 (0x15dbcc) | `59ba2e84d4c66d7ef32ac6ae7c8dfe1c47cdb668ae723a63bc6786989ed49986` |
| `nuttx/nuttx.bin` | 156604 (0x263bc) | `10c7733ddd0ac598b0ddf274aecde819cc445dd56f82120c937e55eef3e7ffd3` |
| `nuttx/nuttx_crc.bin` | 166396 (0x289fc) | `690728ef054c09e1633ae6a5eaf1850aa93f25c3a631436b950e7b0bcc23290d` |
| `nuttx/all-app.bin` | 236028 (0x399fc) | `d7b73c7fdf1275a90621b54e0343d6de31d343f115eb6acd0173fb971a2cf1b0` |
| `bl_crc.bin` | 69632 (0x11000) | `c4b46405a59504dd14b45ba25f95e271ea5d695c87da0a6d50220aede39fb86f` |

Partition fit: `all-app.bin` (236028) == `bl_crc.bin` (69632) + `nuttx_crc.bin` (166396). Content concatenation verified.

**Distinction from board-flashed artifact:**
- Board-flashed (16:04): `all-app.bin` SHA-256 `a92352eeea5ebbab4eb4a7a95fd97a73d950816a3620242be8e8fc141030b5a5` (board-verified)
- Precommit rebuild: `all-app.bin` SHA-256 `d7b73c7fdf1275a90621b54e0343d6de31d343f115eb6acd0173fb971a2cf1b0` (compile/static-verified only, NOT board-tested)

Different hashes expected: source-identical but fresh build timestamp/linker layout may differ; no runtime behavioral difference expected.

### Status

Precommit build/static verification complete. All 23 gates PASS. Source-scope verified: only intended N6/A1/B2 paths modified. No ignored binaries staged. No team-file warnings. A1 source remains board-verified; new rebuild is build/static-verified only. No flash. Board code and SDK-localization tooling were committed as `66b29d1`; documentation commit and push remain.

### Next single minimal action

Commit the synchronized N6 documentation, then push `bk7258-n6-ramvectors` to `fork`.
SDK IRQ bridge not started.
