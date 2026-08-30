# BK7258 phase-one PM idle and T5-Board J-Link recovery

Date: 2026-08-12 GMT+8

## Scope

- Hardware: T5-Board, SWD on P0/P1, firmware download on COM3.
- UART1/COM4 was physically disabled and was not used.
- Runtime inputs: immutable Beken SDK v3.1.1.9 archives plus board-owned
  NuttX adaptation code.
- No NuttX/SDK source, OTP/eFuse, security lifecycle, or debug-lock state was
  modified.

## PM result

Phase one retains CP DVFS/module-clock voting and adds only ordinary shallow
idle. The CP NuttX governor is bounded at `PM_IDLE`; the prepare callback
accepts `PM_NORMAL`, `PM_IDLE`, and `PM_RESTORE` and rejects deeper states.

The non-RTT CP ELF showed this call and instruction path:

```text
up_idle -> pm_idle -> bk7258_pm_idle_handler
          -> clear SCB.SCR.SLEEPDEEP -> DSB -> WFI -> ISB
```

The AP ELF contained the same clear-SLEEPDEEP and DSB/WFI/ISB sequence
directly in `up_idle()`. Symbol checks found no `arch_deep_sleep`,
`pm_state_machine`, or `sys_hal_enter_normal_sleep` in either phase-one ELF.

The ELF wrapper checker now distinguishes runtime-required symbols from
read/write/erase entry points that `--gc-sections` may legally remove when a
profile has no caller. The host ABI test still exercises every operation, and
the ELF gate still requires the SDK startup and raw-permission call paths to
resolve to the project wrapper.

## J-Link and boot-chain root causes

A DWT write watchpoint on P1 stopped in SDK `gpio_ll_input_enable()` /
`gpio_hal_default_map_init()`, called by `bk_gpio_driver_init()`. The program's
own earlier SWD setup called that global GPIO initializer, so it caused one
P0/P1 takeover. This GPIO cause was real, but it was not the entire boot
failure.

The non-MCUboot package also followed a BL2-style handoff even though the
payload was the raw CP image. The reconstructed, board-owned BL1 now has an
explicit `BL1_USE_BL2=0` path: it validates the CP vector and image marker,
sets the CP stack limits, performs final security/cache cleanup, reasserts SWD,
then waits immediately before the direct CP branch. This changes project BL1
source and its generated binary; it does not patch or replace bytes inside a
vendor bootloader binary.

The final SWD-enabled correction is bounded:

- SWD setup is direct and idempotent; it does not invoke global GPIO init.
- `--wrap=gpio_hal_default_map_init` suppresses only the SDK all-pin default
  map. `bk_gpio_driver_init()` still performs HAL/mailbox/IRQ initialization,
  and GPIO clients still configure their own pins.
- P0/P1 maintenance changes only mask `0x6c` to function-control value `0x48`,
  preserving live and read-only status bits. A three-second final DWT watch
  observed no writes.
- BL1 establishes CPU0/group-1 SWD before the chain. A direct profile
  reasserts authorization, routing and pins after final handoff cleanup, then
  waits for release magic `JLNK` at `0x2809f7f0` immediately before CP. The
  APB and AON watchdogs are stopped before this deliberately unbounded hold.
  In an MCUboot profile, BL1 does not hold before SRAM BL2; BL2 performs the
  equivalent final gate after image authentication and cleanup.
- GPIO1 is shared with P1 on this board variant. Board LED lower-half
  registration is therefore skipped whenever P0/P1 SWD or UART1 owns it.
- The RTT debug profile omits UART1 and keeps CP awake for initial attach.
- NuttX watchdog automonitor was removed only from the RTT debug configs. The
  watchdog driver remains registered, but an 8-second automonitor cannot turn
  a long debugger halt into NMI.

Earlier diagnostic attaches isolated these causes. The final cold-start proof
below supersedes the earlier attach-only result.

## Runtime progress corrections

Two independent runtime issues remained after SWD itself was stable:

- The transport-only AP profile requested the 320 MHz startup vote intended
  for radio profiles. Gating that vote on the radio configurations removed AP
  bootstrap error `0x1a`; the final APBS state is READY with error 0.
- CP called `up_timer_set_lowerhalf()` without `CONFIG_TIMER_ARCH`; in that
  configuration the API is an empty inline function. SysTick therefore stayed
  disabled, `nxsig_usleep()` never completed, `board_late_initialize()` did
  not return, and the CP RPMsg worker could not process AP notifications. The
  non-`CONFIG_TIMER_ARCH` path now directly attaches the SysTick IRQ, programs
  reload/current/control, and calls `nxsched_process_timer()` periodically.
  The `CONFIG_TIMER_ARCH` lower-half path is unchanged.

## Build and board evidence

The expanded configuration foundation provides P0/P1 or P20/P21 SWD, CP/AP
target selection, optional boot hold, NONE/RTT/UART0/UART1/UART2 console,
per-UART baud/data/parity/stop settings, UART0 CTS/RTS, and two UART2 pin
groups. The paired builder rejects known SWD/UART/LCD/camera/touch/USB pin
collisions and derives BL1/BL2 constants from the CP defconfig rather than a
profile name. Mandatory SDK/IPC/PM/AP lifecycle bringup moved to idempotent
`board_late_initialize()`; procfs/MTD/filesystem setup remains in the
application hook.

Host/build verification for this refactor included:

- complete `cp_nsh_rptun_rtt + ap_smp_rptun` dual build and package: PASS;
- BL1 P0/P1 SWD + silent hold and BL1 P20/P21 + UART2: PASS;
- BL2 P0/P1 SWD + silent hold: PASS;
- normal UART1, UART0 at 921600 with CTS/RTS, and UART2 P30/P31 plus P40/P41
  at 1500000 7E2: PASS;
- partition, factory-layout, SDK-wrapper and RPTUN ELF gates: PASS.

The final direct-debug package records `BL1_USE_BL2=0`, `BL1_SWD_ENABLE=1`,
pin group 1, CP target, boot hold enabled, and UART value 3 (silent). Its CP
raw-image SHA-256 is
`f395f45bc90fd25f7f4c5d3ae752e872e1185dfe7cb760b36187d64de9eb88de`;
the CP CRC-expanded segment is
`e9738765becf7c4f9510119451e3cf13c572795164863390a04840c63126ef9e`.
The AP segment remained unchanged at
`f5200cbb68f79a776f1376d756eac95fbab918f64baeaf8b1f421894ccd18158`.

The restored signed RTT build
`cp_nsh_rptun_rtt_mcuboot + ap_smp_rptun_mcuboot` completed with status 0.
It passed SDK checksum, BL1/BL2, MCUboot signing, partition, factory-layout,
and RPTUN checks. The signing key was an external temporary development key;
no private material is stored here.

The exact final board-written image is recorded in workspace log
`logs/bk7258-auto-debug/20260812-204429`. Its three sparse ranges were:

```text
boot          0x000000 length 0x11000
CP            0x011000 length 0x3e000
AP            0x165000 length 0x20000
```

COM3 connected successfully, all three `WriteFlash` operations passed, and
BKFIL ended with `Writing Flash OK` and `All Finished Successfully`. No BL2,
slot B, LittleFS, configuration or calibration-tail range was written. COM4
was not opened; zero serial bytes are expected for this silent RTT profile.

After the automatic reboot, the first J-Link attach proved the BL1 final gate:

```text
PC=0x020002ba  VTOR=0x02000000
SWD function=0x22  P0/P1 control=0x00050048  route=CPU0
release word 0x2809f7f0=0
```

The session wrote `0x4a4c4e4b` to the release word and resumed. More than 12
seconds later an independent attach still found P0/P1 and CPU0 SWD intact and
recorded:

```text
CP VTOR=0x28010800
SysTick CTRL=0x00010007 LOAD=0x00124f7f CURRENT=0x000d379f
startup trace count=7, final stage=0x203
platform_initialized=1, watchdog_initialized=1
RPTUN state=4 (CONNECTED), flags=0x00007fff, error=0
APBS state=2 (READY), error=0
CPU2 state=8 (RUNNING)
SMP state=4 (PASSED), error=0, online_mask=3
```

Both RPMsg notification sequence counters advanced and the CONNECTED_ONCE bit
was set, so this is CP/AP bidirectional transport progress rather than AP-only
startup. The target was resumed before the final J-Link session exited. The
probe's old-firmware cache warning remains a probe limitation; it did not
prevent these read-only state checks.

## Proof boundary

This closes first-phase shallow PM and the program-caused P0/P1 J-Link attach
failure. It does not claim full v3.1.1.9 SDK PM equivalence. Coordinated
low-voltage standby still requires CP voting, both AP WFI reports, restricted
AON/mailbox wake sources, IRQ/DMA/SysTick handling, SLEEPDEEP entry, and full
wake restoration as one separately verified protocol.
