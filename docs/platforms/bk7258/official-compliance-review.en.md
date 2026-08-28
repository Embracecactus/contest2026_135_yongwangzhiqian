# BK7258 Official Compliance Reassessment

English | [简体中文](official-compliance-review.md)

- Review date: 2026-08-28
- Reviewed commit: `ce435f5ed66f5744339a3dc846405d9f7bc4b93a`
- Reference: openvela `dev-ai-contest-2026` documents 1443, 1444, and 1445
- Method: mandatory interfaces, recommended designs, example directory layouts,
  and BK7258 architecture differences are evaluated separately; source presence,
  configuration reachability, build artifacts, and hardware evidence are not
  treated as interchangeable claims

## Conclusion

The original review found one real optional-feature link defect and one
high-value delivery-documentation defect. It also classified several
recommendations or example layouts as mandatory and missed the three CMake loops
that generate driver source names.

- **Real code defect:** enabling CP `CONFIG_BK7258_TOUCH` calls
  `bk7258_board_cp_devices_initialize()`, but production board code does not
  define it; the only definition is a host-test stub. No maintained defconfig
  enables this option, so current product builds are unaffected. A board
  implementation and a build/link gate are required before enabling it.
- **Resolved delivery-documentation defect:** the former root `README.md` was
  still the competition template and omitted the toolchain installation, SDK
  bundle rebuild, paired build, and artifact naming. It has been replaced by
  matching Chinese and English project descriptions.
- **Resolved documentation-ownership issue:** `docs/bk7258-t5ai/` covered three
  boards despite its name and is now `docs/platforms/bk7258/`. Learner material
  is under `docs/learning/bk7258/`, reverse-engineering records are explicitly
  `bootloader-analysis/`, and the T5AI Core-only probe is under
  `hardware/t5ai-core/probe/`. Active source comments, navigation, and
  verification-document references were updated with the move.
- **Not a defect:** the claim that CMake omits 13 drivers is false. Three
  `foreach` loops generate exactly those 13 source names. Existing CMake/Ninja
  rules also contain the enabled `bk7258_aud.c`, `bk7258_i2c.c`,
  `bk7258_mic.c`, and `bk7258_rtc.c` objects.

Documents 1443, 1444, and 1445 therefore should not all be downgraded to 🟡.
The core interrupt adaptation in 1444 remains ✅. Documents 1443 and 1445 should
show functional compliance and literal template-layout alignment as separate
dimensions.

## Architecture differences and boundaries

### Paired CP/AP product, not a single-image product

Each physical board owns an `openvela.conf` that selects a compatible CP/AP
configuration pair and one partition CSV. `tools/bk7258/bk7258.py build` invokes
the official `build.sh ... --cmake` once per role, then verifies the board, SDK
profile, memory, and storage topology of the pair. The CP `openvela_cp` profile
uses `nsh_main` and enables `CONFIG_SYSTEM_NSH`. The accurate statement is thus
that there is **no single-image product directory literally named
`configs/nsh`**, not that the project lacks an NSH baseline.

`configs/nsh` is a usual/default convention in the official guide. BK7258 keeps
`configs/openvela_cp` and `configs/openvela_ap` because renaming either role to
`nsh` cannot describe the other required image. A future `configs/nsh` target
could only be an additional CP-only diagnostic target, not the normal product.

### Startup responsibilities follow physical ownership

CP owns system startup. Its `__start()` establishes the C runtime, early serial
output, and the optional boot-frequency setting, and the CP lifecycle later
releases AP. AP `__start()` still establishes its local vectors, FPU, AP SMP
exclusive regions, `.data`, `.bss`, and `nx_start()`, but does not repeat
whole-SoC clock initialization. Maintained AP defconfigs explicitly disable
`CONFIG_DEV_CONSOLE` and `CONFIG_SERIAL`, so there is no AP
`arm_earlyserialinit()` call. This prevents two images from competing for the
global clock and console owner.

### Three active board-lifecycle stages

The boards select `CONFIG_BOARD_LATE_INITIALIZE`, not
`CONFIG_BOARD_EARLY_INITIALIZE`. BL1 and CP `__start()` perform the pre-idle SoC
work. Board devices and SDK operations that require the scheduler begin at
`board_late_initialize()`, followed by `board_app_initialize()` and
`board_app_finalinitialize()`. There is currently no board operation that must
run before the idle task, so an empty early hook is not registered. A future
requirement in that phase must reopen this decision.

### Shared linker templates with board-generated layouts

All three boards reuse `boards/bk7258/common/scripts/ld.script` and
`ld_ap.script` to avoid copying the same SoC section layout. Each board still
selects its own partition CSV in `openvela.conf`. The build generates
`BK7258_PARTITION_HEADER` and `BK7258_PARTITION_LINKER`, and both Make and CMake
consume those generated inputs. Sharing the template therefore does not prevent
per-board memory layout. A board-specific script is needed only when the section
organization itself differs.

### `arch_timer` and the complete IRQ table are deliberate choices

Document 1443 supports both `arch_alarm` and `arch_timer`; it describes the
former as preferred, not mandatory. BK7258 uses SysTick from a fixed 32 kHz
source. A static assertion requires exact divisibility by `CLK_TCK`, and
`systick_initialize(false, 32000, -1)` registers the timer lower half. Coordinated
standby also performs periodic-tick phase compensation. This implementation does
not claim tickless or independent `arch_alarm` support.

BK7258 has 80 logical IRQ slots: 16 system exceptions plus 64 SDK sources. The
SDK bridge maps sources 0 through 63 one-to-one. The minimal-vector-table section
in document 1444 is a memory optimization for large sparse IRQ spaces, not a
compliance gate. A complete 80-entry table is a bounded architecture choice.

## Item-by-item verdict on the original review

| Original claim | Verdict | Reassessment |
|---|---|---|
| CMake has 13 fewer drivers than Make | ❌ False | Three `foreach` loops generate `AUD/GPIOE/I2C/I2S`, `MIC/RTC/SARADC`, and `SDMADC/SPI/QSPI/CAN/TIMER/TRNG`, exactly covering the literal filename difference; current Ninja rules show enabled objects entering `libarch.a` |
| `arch_alarm` is not used | 🟠 Fact correct, severity overstated | The port uses `arch_timer`; document 1443 supports both models and only prefers alarm. The fixed 32 kHz source, divisibility assertion, and standby compensation provide an explicit rationale |
| AP `__start` lacks clock and early serial initialization | 🟠 Fact correct, ownership difference | CP starts AP, and maintained AP configurations have no serial console; AP still performs its required local C/SMP/FPU/VTOR startup work |
| Minimal vector table is not enabled | 🟠 Fact correct, not a defect | Document 1444 presents it as an optimization; an 80-slot one-to-one SDK mapping is bounded and stable |
| `up_prioritize_irq` is not wrapped by `CONFIG_ARCH_IRQPRIO` | 🟠 Low-risk style difference | The official requirement is to provide the function when IRQ priority is enabled; it does not require removing the symbol otherwise. Maintained feature profiles select `ARCH_IRQPRIO` |
| `bk7258_irq.h`, `bk7258_lowputc.h`, and `bk7258_start.h` are absent | 🟠 Filename fact, not an interface defect | Document 1443 shows a typical tree rather than a mandatory file list. Contracts live in `include/irq.h`, `bk7258_console.h`, and role-private startup code |
| Kconfig has no chip-model `choice` | 🟠 Fact correct, not applicable | This custom chip directory supports BK7258 only; the official `choice` example is useful when one family selects among several models |
| CP/AP must be paired | ✅ Correct | The normal product is paired and the delivery documentation must say so; CP itself remains an NSH-starting image |
| `board_early_initialize` is disabled | ✅ Correct and intentional | No pre-idle board work exists; BL1/CP startup owns early SoC work and late/app/final are the active board phases |
| No `configs/nsh` | 🟠 Literal directory fact, severity overstated | `openvela_cp` enables NSH. The difference is official conventional naming, while the normal product also requires `openvela_ap` |
| Thirteen empty config directories remain | ❌ Not a repository defect | They were untracked local empty directories. Git cannot carry empty directories, so a remote evaluator does not receive them |
| Missing `etc/group`, `etc/passwd`, and `RCRAWS` | ❌ Not a current functional defect | Maintained configs do not enable login/account databases. The ROMFS contains only `rc.sysinit` and `rcS`, for which `RCSRCS` is correct; the official files are extensible examples |
| Linker scripts are centralized under `common` | ✅ Fact correct, conclusion false | Shared templates are combined with per-build generated partition headers/linker inputs; all three boards retain independent CSVs and board `Make.defs` entry points |
| `bk7258_board_cp_devices_initialize` has no production definition | ✅ Real defect | It appears only when `CONFIG_BK7258_TOUCH` is enabled, which maintained defconfigs do not do. It must be implemented before enablement; documentation cannot replace that code fix |
| All three boards have identical, marker-only rc scripts | ✅ Correct and documented | Each physical board continues to own its ROMFS input. `boards/bk7258/CONFIGS.md` explicitly describes the current scripts as marker-only |
| The `ld.script` header contains a stale path | ✅ Correct, resolved | This was a comment-only error and did not affect linking |
| Root README is still the competition template | ✅ Correct, resolved | The new Chinese and English project descriptions cover overview, track, layout, reproduction, and AI Coding usage |
| Wrapper-injected environment variables break reproduction | 🟠 Partly correct | They are validated, fail-closed internal contracts and users should not set them. The former README really did omit toolchain installation and SDK rebuild steps; both are now documented |
| The `bk7258-sdk` group is excluded from `default` | ❌ False | The manifest project has no `notdefault` marker. Repo's `default` group includes projects without `notdefault`; the README names both groups explicitly to remove ambiguity |
| `prebuilt` is an empty shell | 🟠 Fact correct, reproducible design | Git tracks installation instructions only. `toolchain install` downloads from the official Arm URL and verifies the pinned SHA-256 before populating the ignored directory |
| Artifacts do not use `vela_ap.bin` | ✅ Fact correct, multi-image design | Names follow partition roles so CP, AP, BL1, BL2, and pair artifacts cannot be confused; the README documents the names |
| Team `tests/` has no linkfile mapping | 🟠 Fact correct, no fix required | Tests run directly from the team repository and compile active sources; they are not part of the NuttX source tree. The root README gives the entry point |
| SDK bundles are not committed | ✅ Fact correct, third-party boundary | The manifest pins source and revision; bundles are rebuilt deterministically from a clean SDK checkout. The README documents rebuild and verification |
| Hardware evidence and AI logs both live under `logs/` | ✅ Low-risk organization issue | Only `logs/lijian/` is competition-format AI log data. The seven `logs/bk7258-*` trees are early hardware evidence. New structured evidence belongs under `progress/verification/` |

## Open items

1. Implement `bk7258_board_cp_devices_initialize()` for any physical board that
   will enable `CONFIG_BK7258_TOUCH`, and add a matching build/link test.
2. If an evaluator requires a literal directory match despite the architecture
   note, add a clearly labeled CP-only diagnostic `configs/nsh`; do not present
   it as the normal paired CP/AP product configuration.
3. Early hardware-evidence directories may later move to `progress/evidence/`,
   but historical references require a separate link audit. This change only
   clarifies their boundary.
