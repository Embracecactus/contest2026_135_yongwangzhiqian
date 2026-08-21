# Architecture

Last reviewed: 2026-08-16

## System context

The physical target is a three-core Arm Cortex-M33 BK7258. CPU0 is the CP boot
master. Physical CPU1 and CPU2 form one AP NuttX SMP cluster. The source
Tier-1 bootloader hands the CP image control; CP initializes shared hardware
owners and releases the AP image.

The official adaptation hierarchy is exactly Architecture (upstream NuttX) ->
Chip/SoC (BK7258-intrinsic mechanisms) -> Board (pins, bindings, external
devices and bring-up).  `vendor_common_glue` and build/migration tags are
internal responsibility labels, not additional public layers.

Canonical overview: [BK7258 porting report](../docs/bk7258-t5ai/porting-report.md).

## Components and ownership

| Component | Owner and responsibility |
|---|---|
| BK7258 logical board and physical variants | `board/bk7258` is the shared BK7258 platform. Exactly one physical binding supplies electrical facts and fitted-device capability; storage and boot algorithms contain no board-name branch. See [ADR-023](decisions/ADR-023-bk7258-platform-board-variants.md). |
| BK7258 build and package boundary | `tools/bk7258/bk7258.py build|sdk|package|verify` is the only public tool. Six internal domains own build orchestration, SDK transactions, partition layout, image bytes, deterministic `.bkpack` and trust. CP/AP use official `build.sh`; project BL1/BL2 build out of tree. The team-manifest ARM prebuilt is the single compiler source for all stages. There is no dual-image shell, postbuild hook, framework, executor or compatibility alias. |
| BK7258 build profiles | Each explicit config directory contains `defconfig` and `profile.conf`; the latter owns board, role, compatibility and SDK profile. `build` requires CP config, AP config, `--boot direct|mcuboot`, partition CSV and jobs. MCUboot derives build-local defconfig overlays instead of tracked boot-mode copies. See [ADR-028](decisions/ADR-028-bk7258-single-cli-data-owned-build.md). |
| BK7258 initialization layers | `src/bk7258_platform.c` owns mandatory SDK/IPC/PM/AP lifetime initialization, `src/bk7258_bringup.c` owns application-facing procfs/MTD/filesystem registration, and the selected physical-board hook owns attached LCD/touch/camera validation and registration. `board_late_initialize()` and `board_app_initialize()` are thin NuttX entry points. |
| BK7258 microphone | One AP NuttX audio lower-half owns DMA, ADC and worker lifetime.  The selected board descriptor supplies fixed analog topology: T5AI-Core is MICP1/MICN1 mono; T5-Board is MICP1/MICN1 plus MICP2/MICN2 stereo.  Kconfig supplies product defaults for sample rate, analog/digital gain and buffering, while applications negotiate supported formats through `/dev/audio/pcm0c`.  The immutable AP SDK's separate AUDIO power-domain (`122`) and audio-clock (`30`) calls are link-wrapped into one generation-scoped CP-owned composite resource, because the native CPU1-to-CPU0 SDK PM mailbox is not an owner after RPTUN takes that mailbox. |
| BK7258 speaker DAC | One AP NuttX playback lower half owns repeat GDMA, the two-frame DTCM ring, DAC and APB/worker lifetime at `/dev/audio/pcm0p`.  The selected board owns only the PA electrical binding: T5-Board P28 and T5AI-Core P39, both active-high with board-owned delays.  The first accepted contract is mono S16/16 kHz/320 samples/eight explicit APBs.  `RESERVE` through successful `RELEASE` owns the Audio 480 MHz SDK-tier vote; the bounded scheduling order is feeder 246 > refill worker 245 > board-default transport 225.  An optional chip-private hardware-EQ extension deep-copies four raw signed-22 coefficient banks before hardware creation, applies them while the initialized DAC is muted, and deconfigures them before DAC teardown.  It advertises no standard NuttX equalizer capability and owns no board preset.  Speaker and microphone validation remain mutually exclusive until shared full-duplex clock ownership is designed. |
| BK7258 SARADC | The AP chip lower half publishes `/dev/adcN` through the standard NuttX ADC ABI.  One open session owns only the selected channel's GPIO mapping; each trigger takes, initializes, explicitly configures, starts, reads, stops, deinitializes and releases the shared ADC controller before delivering one sample to the upper FIFO.  CP owns the boot-lifetime GPIO runtime, SDK IRQ bridge and ADC mailbox server.  The selected board alone supplies pin/channel/electrical meaning: T5-Board binds active-low SW5 at P12/ADC14.  The generic validator contains no board endpoint assumption. |
| BK7258 JPEG M2M decoder | One AP chip-level owner publishes the existing synchronous hardware JPEG decoder through the standard NuttX V4L2 M2M codec upper half.  `/dev/video1` accepts single-planar baseline JPEG on the OUTPUT queue and returns tightly packed YUYV on CAPTURE.  The initial contract is USERPTR-only and single-open; a dedicated one-thread work queue serializes the SDK singleton, while STREAMOFF and close synchronously cancel and return every queued buffer.  A bounded local parser admits only the reviewed SOF0 single-scan three-component subset before the immutable SDK sees input, and a grow-only guarded bounce satisfies its hidden `bytesused + 2048` DMA read.  The 32 x 16 baseline 4:2:2 fixture, negative/recovery/drain lifecycle and resource cleanup are board-verified.  This component owns no camera, LCD, board pin, DMA2D stage or RGB conversion. |
| Tier-1 bootloader | The executable BL1 is project-owned and built out of tree. The official Beken bootloader is reverse-engineering evidence only and never an image input. Direct BL1 jumps to CP; signed BL1 verifies CSV-declared Manifest/BL2 A/B. |
| BK7258 integrated Flash and storage | The selected eight-column CSV is the sole Flash geometry, Artifact, Policy and storage-topology source. `layout.py` writes build-local SDK/C/LD derivatives. `onchip-persistent`, `removable-block` and `fixed-block` are system topologies independent of applications and board names. See [ADR-029](decisions/ADR-029-bk7258-storage-and-boot-topologies.md). |
| CP NuttX on CPU0 | On-chip Flash owner, AP lifecycle supervisor, RPMsg peer, Beken Bluetooth Controller owner, Wi-Fi RF/PHY/MAC/WPA/controller owner, PSRAM hardware/PM owner. A selected system topology may expose persistent data through CP; applications do not own the medium. See [ADR-025](decisions/ADR-025-bk7258-radio-lifecycle-boundary.md). |
| AP NuttX SMP on CPU1+CPU2 | Stock NuttX scheduling/Host/services; logical CPU0 owns RPMsg/Bluetooth/Wi-Fi gateways, logical CPU1 is a business and socket producer |
| Beken SDK | The single manifest project tagged `bk7258-sdk` owns source, revision and base version. Profiles follow `<role>[-<variant>].config`, declare NuttX-owned closure omissions, and carry one accepted deterministic bundle-tree hash. `sdk rebuild` builds a temporary clean official checkout, extracts the actual `app.elf` link inputs, patches the UART archive, exports the official generated partition header and atomically replaces the ignored bundle plus profile hash. There is no version constant, registry, set/lock, manifest/provenance pair or Make/CMake library-name map. |
| Beken `bk_idk release/v2.0.1` | Read-only official reference: its `docs/bk7258/**` pages and generic security tools provide BK7258 Secure Boot semantics/packaging evidence; its buildable `projects/security/**` examples are BK7236-only single-core samples. Never a runtime archive or source replacement; see [ADR-017](decisions/ADR-017-bk7258-official-secureboot-source-crosswalk.md) |
| Windows/WSL2 tools | Build, sparse/factory download, UART/J-Link evidence, and no-GUI BLE client |
| Historical N15 OTA evidence | The former custom inactive-slot writer, dual-bank journal and trial/rollback lifecycle were physically exercised, then retired from active source. Their ADRs and verification records are historical evidence, not current firmware architecture. |
| N16 Wi-Fi (accepted architecture, complete for STA scope) | Official v3.1.1.9 radio/controller and DHCP client remain on CP; AP uses the official vnet proxy plus a repository-owned lease/netdev adapter to native NuttX `wlan0`/IPv4/sockets; vendor AP lwIP is excluded |
| BL1/BL2/MCUboot chain | Project BL1 verifies Manifest A/B and project freestanding BL2 A/B. BL2 uses standard MCUboot multi-image trailers with direct-XIP revert, but exposes one complete same-slot, same-version, same-counter and same-trailer-state CP/AP pair per `boot_go()` attempt. Public-only C roots are generated in the build tree from explicit PEM inputs; signed `.bkpack` evidence embeds public keys and signatures rather than a separate trust contract. The chain remains software-rooted: OTP/eFuse provisioning and hardware monotonic rollback are not claimed. See ADR-031. |
| CP debug and console transport | SWD route, target core and console transport are independent configuration axes with paired-image pin-conflict gates. SWD supports P0/P1 or P20/P21; console supports NONE, RTT or UART0/1/2 with explicit frame/baud and route settings. Direct profiles stop APB/AON watchdogs and hold in BL1 after final cleanup; release magic immediately precedes the CP branch. MCUboot profiles hold at the equivalent BL2-to-CP boundary. Board-verified T5-Board configurations use P0/P1 CP SWD with either RTT or, for the dedicated Audio one-shot profile, UART0/COM3; UART1/COM4 is omitted. They suppress only the SDK all-pin default-map pass that would overwrite the route. P20/P21 and other UART routes are compiled but not board-verified. |
| PM and timer policy | NuttX remains the PM owner and the SDK is a leaf hardware service. Ordinary idle uses clear-SLEEPDEEP then DSB/WFI/ISB.  CP physical CPU0 and AP-primary physical CPU1 use fixed external-32 kHz scheduler SysTick routes; for timer accounting, DVFS refreshes their role-local DWT conversion while the scheduler source remains fixed. Coordinated standby is board-owned but follows the v3.1.1.9 protocol: CP owns the request, PWC mailbox exchange and both-AP vote barrier; each AP checks its vote and pending IRQ/DMA state, saves/stops SysTick, publishes AON WFI state, preserves the mailbox wake path, enters SLEEPDEEP and restores in official wake order. CP uses a bounded RTC wake and a hard-IRQ arch-timer proxy to restore whole ticks plus the saved sub-tick phase; one real one-shot restore is board-verified. Missing votes, pending work, stale generations, mailbox errors or restore errors fail closed; no core may independently claim low-voltage standby. AP standby still lacks AON elapsed-time compensation, so complete CP/AP time continuity is not claimed. |

## Primary data flows

- Direct boot: legacy BootROM → board-owned BL1 direct-vector validation and
  optional final debug gate → CP → bounded AP release → AP SMP READY.
- Signed boot: legacy BootROM → board-owned BL1 → signed Manifest → pinned
  NuttX MCUboot BL2 → signed CP/AP pair → bounded AP release → AP SMP READY.
- IPC: one CP↔AP RPTUN/OpenAMP/RPMsg link; AP logical CPU0 is the
  mailbox/OpenAMP gateway.  Every generation also owns a transport-level,
  generation-qualified NS proof endpoint: AP publishes CREATE, CP binds and
  returns CREATE_ACK, and each core stops its bootstrap scan only after its
  local proof.  The shared lifecycle reaches CONNECTED without depending on
  an optional test, syslog consumer or health supervisor.
- Power management: NuttX requests standby on CP → CP publishes a generation-scoped
  PWC request → both AP cores pass local IRQ/DMA/vote gates and publish AON WFI
  state → CP admits the bounded low-voltage interval → mailbox/RTC wake drives
  ordered AP and CP restore → CP compensates scheduler ticks. Any incomplete
  edge aborts to shallow idle.
- Storage: CP exclusively owns raw flash/MTD/LittleFS; AP reaches it through RPMsgFS.
- Bluetooth: CP owns the official Controller; AP owns the stock NuttX
  Host/GAP/GATT through official pointer IPC and a board lower-half.  CP's
  versioned active flag is authoritative across init/deinit; AP reconciles a
  lost reply only when that desired state already committed, otherwise retains
  UNKNOWN ownership and fails closed.  Repeated Controller validation occurs
  before Host ownership because this NuttX configuration cannot unregister and
  re-register the Host device.
- Wi-Fi: CP owns official RF/PHY/MAC/WPA, the vnet controller and its DHCP
  client; AP logical CPU0 owns the official command/data proxy and a
  repository lease/netdev seam into native NuttX networking. AP does not run
  a second DHCP client. Runtime STA association, lease synchronization,
  gateway ICMP, local TCP/UDP sockets, bounded retained-service coexistence,
  AP-restart rejection and 3/3 controlled RTS recovery are board-verified.
- PSRAM: CP takes the official PM vote and performs the one-shot capacity gate; CP and AP use disjoint role-local heaps.
- Microphone: the application configures and queues NuttX audio buffers on AP;
  the shared lower-half selects mono deinterleave or stereo preservation from
  the board topology, then uses the SDK ADC/DMA engine.  AP PM requests cross
  RPMsg to the CP owner; open and close acquire/release the same composite
  AUDIO resource, and worker quiescence precedes teardown or buffer return.
- Speaker: the application primes explicit NuttX audio APBs; repeat GDMA drains
  a two-frame ring into the DAC.  Each DMA interrupt wakes the refill worker,
  which commits the next frame before publishing `DEQUEUE`; the bounded feeder
  requeues one APB, and `STOP` or `FINAL` quiesces the worker before PA/DAC/DMA
  reverse teardown and the Audio frequency release.  When the private DAC-EQ
  extension is selected, its shadow is applied after DAC init/mute but before
  DMA/DAC/PA start, and is deconfigured/read back after quiescence but before
  DAC deinit.
- SARADC: `ANIOC_TRIGGER` enters the AP chip lower half, which sends
  acquire/init/config/start/`bk_adc_read`/stop/deinit/release to the CP SARADC
  mailbox server.  The unsupported `bk_adc_single_read` operation is not used.
  The CP SDK ISR completes the sample; AP publishes it through `au_receive`,
  and the NuttX ADC upper half returns the packed channel/value message to
  `read()`.  Session-level GPIO mapping and per-trigger controller ownership
  are released before the last close completes.
- JPEG M2M: an application negotiates JPEG OUTPUT and YUYV CAPTURE formats on
  `/dev/video1`, queues one USERPTR buffer to each side and starts both queues.
  The codec worker validates the complete compressed frame, copies it into a
  zero-guarded DMA bounce, runs the single hardware decoder, then returns both
  V4L2 buffers exactly once.  Malformed input returns an error pair without
  touching hardware; a hardware decode failure faults and rebuilds the unique
  backend before another job.  STREAMOFF or close waits for any synchronous
  SDK operation and drains residual buffers as errors.
- Signed-image selection: CP/AP are one launchable pair. The board-owned MCUboot BL2 exposes only
  one physical slot to each `boot_go()` attempt and requires the CP result and
  AP vector to come from that same slot; a cross-slot-only state fails closed.
  Primary CP/AP and `s_app` remain equal-length contiguous pairs selected by
  one official-style Flash remap decision; persistent and immutable ranges
  are outside both executable spans. CP derives the physical active slot from
  the retained remap registers, stages only the opposite pair with CP sector
  zero committed last, and confirms the two standard `image_ok` trailers after
  application health acceptance. There is no private trial journal; transport
  and health policy remain application responsibilities.

## Persistence and data lifecycle

- Persistent storage is a system service. On-chip persistence currently uses
  CP MTD/LittleFS without boot-time autoformat; removable and fixed block
  topologies use board-exposed media. Applications receive a data directory
  and do not select the medium, filesystem, partition or cross-core transport.
- Bluetooth base MAC/calibration records are created through the official first-calibration path and persist in flash.
- RPMsg endpoints and AP-local state are generation-scoped; AP restart invalidates stale transport state.
- The N14 upper PSRAM half is tested at boot but has no general runtime
  allocator or persistence semantics. The retired N15 validation transfer
  window has no active consumer.
- ADR-003's append-only logs, scratch sector, metadata ABI, and SRAM copy
  closure are retired research artifacts. Their 32,915-case model remains
  evidence for the rejected alternative; the mutation gate is zero and no
  board consumed that ABI.
- The three maintained CSVs retain the previously verified initial BL1,
  Manifest/BL2 A/B and CP/AP A/B geometry. Their storage region is either
  persistent_data or reserved according to topology; all sizes remain
  compile-time CSV inputs. The immutable calibration tail remains explicit.
- ADR-004 records the one-time physical layout migration. ADR-005, ADR-006
  and the N17 format-3 journal remain historical design/evidence only; their
  runtime readers, writers and policy sector have been removed.

## External dependencies

- openvela/NuttX sibling checkouts in the workspace, treated as official read-only inputs.
- Manifest-pinned Beken SDK v3.1.1.9 fork source for bundle regeneration, and
  content-addressed v3.1.1.9 CP/AP bundles for runtime linking, adaptation and
  board verification; see ADR-027.
- Beken `bk_idk release/v2.0.1` only for read-only BK7236/BK7258 secureboot source review. BK7259 and `release/v4.0.1` are retired and prohibited by [the project rules](RULES.md).
- Windows BKFIL/Beken loader, COM serial devices, and SEGGER J-Link for physical-board operations.
- Product capability reference: [Beken BK7258](https://www.bekencorp.com/index/goods/detail/cid/60.html).

## Security and privacy boundaries

- Never store credentials, device-unique private material, or unredacted private production records in project memory or logs.
- The Bluetooth first release has no pairing/bonding/security claim.
- Wi-Fi credentials are runtime-only secrets: never place them in defconfig,
  repository logs or project memory, and never print them unredacted.
- Generic pointers never cross CP/AP through RPMsg; only explicitly reviewed vendor pointer-IPC contracts may do so.
- The active development board must remain recoverable for later driver work.
  OTP/eFuse, secure-boot enable, lifecycle/JTAG locks and hardware rollback
  counters are outside normal firmware and validation authority.
- Normal firmware may not write or erase BL1 Manifest or BL2 partitions.
  There is no active N17 policy sector or field-update mutation path. Any
  future updater must define a new authorization and recovery boundary before
  gaining Flash-write authority.

## Known constraints and technical debt

- T5AI-Core V1.0.1 remains the compatibility default. T5-Board is physically
  verified for COM3 sparse download, P0/P1 CP SWD, BL1/BL2 final hold/release,
  CP/AP startup, CPU2 SMP, bidirectional RPTUN, DVP camera, TF one-/four-bit
  storage and the bounded Audio DAC lifecycle.  Its P12/ADC14 SARADC path and
  released baseline are physically verified, while the SW5 active-low and
  return transitions remain pending.  Its bounded JPEG V4L2 M2M USERPTR
  lifecycle is also physically verified.  P20/P21, alternate UARTs and RGB
  LCD remain compile-only until tested in compatible profiles.
- N14 exposes only 128 KiB CP and 640 KiB AP role-local PSRAM heaps. The remaining regions are reserved by policy.
- PSRAM is non-cacheable; DMA/cache-coherency and performance tuning are deferred.
- AP automatic recovery is disabled by default; the verified baseline is detection plus bounded manual recovery.
- The official v3.1.1.9 single-offset AB remap was incompatible with N14's
  old layout. N15-M completed the owner-authorized ADR-004 migration and full
  retained-service board regression.
- Executable images use 32+2 CRC-expanded physical coordinates, while
  `bk_flash_*` data APIs use raw offsets. The canonical layout/verifier must
  cross-check every conversion and reject old/new layout mixing.
- Partition contracts are generated under `out/bk7258/<config>/generated`.
  Classic Make, CMake, linker preprocessing, image and package stages consume
  the same explicit CSV and generated header/linker paths.
- Partition/MTD composition and SYS_RF/SYS_NET storage mapping are board-owned.
  Chip startup uses linker `_vectors`, AP lifecycle accepts a validated image
  descriptor, and radio lifecycle accepts storage callbacks; chip code does
  not include a board image/partition contract.
- JPEG and temperature validation are command-triggered through `bkvalidate`.
  MIC, AUD, SARADC, TF and other frozen validation profiles still contain
  production or board bring-up auto-start paths, so validation policy remains
  explicitly `mixed-legacy` until P9b.
- Product metadata, resource graphs, source snapshots and the isolated
  executor are retired. `bk7258.py build` directly orchestrates the official
  CP/AP CMake builds. The accepted raw pair and unsigned package are real host
  artifacts but are not thereby signed or hardware-verified.
- BL2 XIP reads must never extend past the valid CRC-expanded payload. A test
  that copied 128 KiB from an 8 KiB BL2 package falsely appeared to show a
  64 KiB SRAM-bank limit; a complete 128 KiB logical/136 KiB physical CRC
  package crossed `0x28030000` and booted successfully. Copy length and
  package span are therefore one contract.
- Exact v3.1.1.9 public normal and A/B reset paths both start with vector MSP
  `0x28030000` and program `MSPLIM = 0x2802f800`. These are boot-stack bounds,
  not a partition of all SRAM or a limit on later BL2/AP allocations.
- The BK7236 v2.0.1 security documentation/source is the active same-Armino
  semantic reference: immutable BL1 authorizes Manifest/BL2, MCUboot
  authorizes later signed images, and TrustEngine/OTP/EFUSE surround the
  chain. It is single-core evidence; BK7258 Manifest bytes, secure registers,
  version-counter ABI and CP/AP mapping remain unproven. See
  [ADR-022](decisions/ADR-022-bk7258-secureboot-bk7236-semantic-port.md).
- MCUboot verification runs on the 34/32-decoded XIP stream and can exceed the
  BL1 watchdog's short recovery window. The board-owned BL2 flash-read wrapper,
  and BL1 only while it executes its one P-256 Manifest verification, use a
  60-second watchdog period; BL1 restores its ordinary period before acting on
  the result. This is a timing boundary, not a waiver for an infinite boot hang.
- N15 R1/R2, format-2 and N17 format-3 lifecycle evidence is historical. The
  former A-to-B-to-A run does not authorize future Flash writes, and the
  corresponding runtime code has been retired. The preserved BL1/MCUboot
  chain authenticates boot images but is not itself a field-update service.
- Official v3.1.1.9 Wi-Fi teardown is incomplete: CP `wifi_deinit()` is
  unsupported and the AP proxy does not close its mailbox channels. Until a
  separate lifecycle design is verified, AP-only restart must fail closed
  while Wi-Fi is active and whole-chip reset is the recovery boundary.
- Official Bluetooth init/deinit is symmetric, but its CP IPC worker always
  reports success without checking the real return.  The board wrapper must
  therefore use CP-owned committed state rather than the vendor event as its
  ownership proof.  Full NuttX Host unregister/re-register remains outside the
  supported lifecycle; do not replace it with forced worker deletion.
- The immutable CP Wi-Fi archive consumes selected `malloc()` blocks as
  zero-initialized state. The board compatibility layer therefore zeroes
  allocations only for the PID executing `bk_wifi_init()`; concurrent CP
  threads retain normal NuttX allocation semantics. This boundary must not be
  broadened for another component without separate allocation evidence.
- The AP Wi-Fi profile uses NuttX's independent 16 KiB FS heap for
  VFS/RPMsgFS/socket metadata. This prevents nested filesystem allocations
  from sharing the vendor Wi-Fi/general AP heap; it is profile-scoped and
  requires no NuttX source modification.
- BK7258 SDK tier names describe the shared DVFS request, not one identical
  per-core clock.  BL1 hands off at the default 120 MHz tier; the verified
  Audio request raises AP to 480 MHz while CP observes the corresponding
  divided role-local rate.  The 320/480 tiers give AP their named rate and CP
  effective 160/240 MHz respectively.  Modules must use the board-owned
  SDK-ordered vote layer rather than writing clock registers independently.
- AP coordinated standby currently restores execution but does not add AON
  elapsed time to the AP arch-timer timebase.  In addition, AP logical CPU1
  does not own a second scheduler SysTick/DWT timebase.  The default CPU0
  affinity protects the validated Audio path, but cross-core high-resolution
  monotonic-time behavior and complete AP standby compensation remain future
  work.
- The DAC-EQ ABI transports raw signed-22 values only.  The immutable SDK does
  not document a hardware Q format, transfer-function convention, stability
  policy or safe non-flat preset, and NuttX exposes no standard coefficient
  payload.  The verified all-zero-bank lifecycle must not be presented as a
  frequency-response, pass-through, stability or acoustic result.
- The first JPEG M2M contract is USERPTR-only, single-open, baseline SOF0 with
  one scan and three 4:4:4/4:2:2/4:2:0 components, and tightly packed YUYV at
  the JPEG dimensions.  It has no progressive/grayscale/multiscan support,
  source-change negotiation, scaling, stride selection, DMA2D/RGB565 stage or
  MMAP contract.  Because the current NuttX codec upper half does not expose
  REQBUFS teardown to the lower half, format allocation is frozen after the
  first buffer-size query and renegotiation requires close/reopen.  A single
  32 x 16 baseline 4:2:2 fixture has a bounded board decode/recovery/cleanup
  PASS; that result is not a reference-decoder pixel comparison, broad JPEG
  conformance or a guarantee for the excluded formats and compositions.
