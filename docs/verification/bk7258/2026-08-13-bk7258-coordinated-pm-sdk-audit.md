# BK7258 Coordinated PM and SDK/Tuya Audit Verification

Date: 2026-08-13 GMT+8

Baseline branch: `feat/bk7258-coordinated-standby` (merged)

Follow-up branch: `feat/bk7258-pm-peripheral-coexistence`, based on `178adaf`

State: coordinated-PM baseline merged; the peripheral-coexistence follow-up is
real-board verified for camera/H264, RPMsg, Bluetooth and Wi-Fi DHCP, and is
ready for publication

## Scope and evidence boundary

This checkpoint replaces the earlier shallow-WFI-only claim with a complete
repository-owned port of the Beken v3.1.1.9 three-core coordinated low-voltage
protocol. The official CP PM state machine, BK7258 CP system-PM HAL, BK7258 AP
system-PM HAL and Tuya's `tkl_*`-over-Beken-SDK wrapper architecture were used
as read-only behavioral references. No official SDK or NuttX source was edited.

The key official distinctions retained by the port are:

- CP is the coordinator and waits for both AP cores; three independent WFI
  calls are not a coordinated standby protocol.
- AP shallow idle and AP coordinated low voltage are separate paths.
- Coordinated entry checks votes and pending IRQ/DMA, stops SysTick, publishes
  AON state, restricts wake to the retained mailbox/RTC path and restores state
  in the SDK order.
- Any incomplete handshake or unsafe condition aborts to shallow idle.

## Audit fixes

- Initialized the I2C mutex through the wrapper contract before first use.
- Made delayed DVP H264 replay failures release the SDK handle and PM clock,
  with H264-only fields contained by their configuration guard.
- Reset the IPI self-test elapsed interval for every message.
- Validated the official Flash partition 32-byte name plus big-endian CRC16,
  rejected corruption with `-EILSEQ`, and guaranteed a terminated exported name.
- Made UART post-init failures deinitialize the SDK UART, unmap its pins and
  restore the console route.
- Aligned semaphore/event wrappers with official/Tuya ANY/ALL, clear and timeout
  behavior while keeping every failure bounded.
- Kept target allocation on `kmm_malloc/kmm_free`; libc allocation is now
  available only to the host verifier.
- Updated RPTUN host mocks for PWC/status/generation/PM_WAKE traffic.
- Updated the SDK IRQ verifier to the current common wrapper path and actual
  dispatcher-safe priority policy.

The audit deliberately did not add cache maintenance to non-cacheable AP
SRAM/PSRAM, change SDK-matching Wi-Fi pbuf references, treat the BT bitfield
helper as W1C, shorten the official-style DVFS critical section, invent a USB
unregister API, or weaken fail-closed RPTUN rollback.

## Build and host verification

All of the following completed successfully:

1. Generic driver-check CP/AP unsigned dual image.
2. Camera/H264 signed CP/AP image.
3. Wi-Fi CP/AP unsigned image.
4. Final `cp_nsh_rptun_mcuboot + ap_smp_rptun_mcuboot` signed image, version
   `18.5.28`, security counter `53`.
5. SDK provenance/checksum, partition/factory-layout, CP/AP link, MCUboot
   signing and RPTUN-layout gates.
6. Host RPTUN test suite: `31 passed, 0 failed`.
7. SDK IRQ wrapper verification: `48 passed, 0 failed`.
8. SDK partition-wrapper verifier and `git diff --check`.

The final sparse package contains CRC-decoded-valid BL1, CP, AP, primary BL2
and secondary BL2 segments. Signing used temporary development software keys
outside the repository; this is not OTP/eFuse-rooted secure boot evidence.

## Physical-board verification

Target: T5-Board. Download used COM3 only. UART1/COM4 remained physically off
and was never opened. Sparse flashing wrote only these five executable ranges:

- BL1 at `0x000000`, length `0x11000`
- CP at `0x011000`, length `0x41000`
- AP at `0x165000`, length `0x22000`
- primary BL2 at `0x51d000`, length `0x4000`
- secondary BL2 at `0x53f000`, length `0x4000`

BKFIL reported every WriteFlash operation successful and finished normally.
LittleFS, `usr_config` and the calibration tail were not written. Canonical log:
`/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260813-015254`.

J-Link used the required `STAR`, SWD, 100 kHz profile on P0/P1. It wrote only
the BL2 release magic and then made read-only memory samples; it never halted
the target. The SWD route register read `0x1a` and the release word initially
read zero before `JLNK` was written.

Two live samples proved continuing coordinated cycles:

| Counter | First sample | Second sample |
|---|---:|---:|
| CP completed entries/wakes | 1,241 | 1,836 |
| AP0 deep entries | 4,543 | 6,618 |
| AP0 wakes | 1,384 | 2,039 |
| AP1 deep entries | 1,159 | 1,689 |
| AP1 wakes | 1,154 | 1,680 |

The latest CP diagnostic may contain an AP-timeout abort from a later attempt
while these success counters increase. That is expected: the port records the
most recent fail-closed rejection and does not disguise it as a successful
entry. The two monotonic samples are the hardware evidence that coordinated
entry and ordered restore continue during normal runtime.

## Remaining boundary

- P20/P21 and alternate UART routes are compile-only until matching physical
  wiring is available.
- This run proves the selected RPTUN MCUboot PM profile, not every peripheral
  coexistence combination or long-duration qualification soak.
- Development keys do not establish hardware-root trust. No OTP/eFuse,
  rollback fuse, lifecycle lock or debug lock was changed.
- The baseline checkpoint is merged as `178adaf`. The peripheral-coexistence
  publication candidate is described below.

## Peripheral-coexistence follow-up

The coordinated-PM baseline was merged as `178adaf`. Follow-up work on
`feat/bk7258-pm-peripheral-coexistence` added a repository-owned SDK activity
gate and closed a hardware-only AP1 wake failure without modifying official
NuttX or SDK sources.

The failure was not the mailbox `param0/param1` contract: v3.1.1.9 sends
`param2` and reconstructs source/destination fields on receive. Read-only SWD
evidence instead showed `g_bk7258_ap_smp_pending[1] == 1` while the CPU2
mailbox FIFO/interrupt status was empty and AP1 remained in AON WFI. Ordinary
`up_send_smp_sched(1)` retries therefore coalesced forever. The PM release
worker now replays one physical SMP edge after revoking the stale software
claim, strictly while the CP sleep vote is clear and AP1's AON WFI bit is set.
Normal scheduler sends still coalesce normally.

The signed camera/H264 profile `18.6.4`, security counter `58`, was sparsely
written through COM3. All five executable writes passed; LittleFS,
`usr_config` and calibration were preserved. BL2 was released through the
existing `JLNK` hold and all state collection used P0/P1 SWD without halting or
resetting the running target. The download log is
`/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260813-092220`.

Camera validation finished with result zero, 21,312 H264 bytes, checksum
`0x40d20d9d`, DVFS `120 -> 480 -> 120 MHz`, and both AP activity votes cleared
after capture. Across three live samples AP1 deep entries advanced
`475 -> 1,874 -> 2,471`, wakes advanced `403 -> 1,751 -> 2,327`, and its
heartbeat advanced `0x7f8 -> 0x20fd -> 0x2bd9`. The supervisor stayed HEALTHY
with zero faults, recoveries and consecutive failures. During an in-flight
standby AON was `0xE` and each AP had exactly `aon_sets - wakeups == 1`, which
matches the asserted AP WFI bits; the counters continued advancing rather
than freezing.

Host mailbox tests passed 31/31, BL1-policy and PM-activity tests passed, and
`git diff --check` passed. The Wi-Fi/BLE/RPMsg retained-service MCUboot pair
also completed a full signed build and layout verification.

### Retained-service direct board run

The signed `cp_nsh_wifi_rtt_mcuboot + ap_smp_wifi_mcuboot` pair, version
`18.6.5`, security counter `59`, was sparsely written through COM3. The five
executable writes passed; LittleFS, `usr_config` and the calibration tail were
preserved. The download log is
`/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260813-094052`.
BL2 was released with the existing `JLNK` word. J-Link identified the STAR
core through P0/P1 SWD at 1 MHz and never halted or reset the running target.

RTT was attached at the final CP ELF's `_SEGGER_RTT` address `0x2802b910`.
RTT0 exposed NSH and RTT1 remained the syslog channel; COM4 was not opened.
The following direct target results were observed:

- `bkrpmsgtest all 100 60000` passed all six combinations of idle/load and
  1/64/464-byte payloads. Both AP cores sent and received 100 messages in every
  case with zero errors; heap arena/used/free/largest/block counts were
  unchanged, and the suite ended `BRPT SUITE PASS runs=6 count=100`.
- Bluetooth info passed with a valid non-fallback controller address, ACL MTU
  70 and 20 ACL buffers. A 3-second scan found seven advertisers and passed.
  Post-scan statistics passed with 22 completed HCI commands, 227 received
  events, zero invalid RX, zero receive errors and zero dropped host-number-
  completed events.
- Wi-Fi `status` initially completed with status zero and an idle link. Runtime
  credentials were later supplied only through the live RTT prompt; no
  credential was persisted in the repository or checkpoint records. Two
  bounded connection attempts found the access point, completed 802.11
  authentication and association, and reached WPA `COMPLETED` at approximately
  -58 dBm. CP lwIP then emitted repeated DHCP DISCOVER messages without seeing
  an OFFER. The final CP lwIP statistics recorded 20 IP transmits and 20 UDP
  transmits, zero IP/UDP receives, and zero IP/UDP protocol or memory errors.
  This proves DHCP traffic reached the lwIP output path and no reply reached
  lwIP; the SDK does not populate the inspected link counters, so it does not
  by itself prove that every frame reached the air. Each request ended
  `-ETIMEDOUT`, and the wrapper executed its bounded STA_STOP cleanup, leaving
  link state DISCONNECTED. The host has both an upstream Ethernet link and a
  separate WLAN association to this access point; the WLAN adapter itself has
  a DHCP lease from the Wi-Fi router, proving that the wireless DHCP service
  works for another client. Non-halting SWD reads showed that CP lwIP, CP STA
  configuration and AP/NuttX cache the same valid unicast MAC. The N16 board
  record previously obtained a lease and passed gateway traffic using that
  same persisted MAC. CP MAC data counters retained 8 queued/downloaded,
  single-frame successful sends, zero retry/discard, and RX data with zero
  allocation failure; those aggregate counters cannot attribute every frame
  specifically to DHCP. WPA completion still rules out the credentials.
  Gateway ping and socket echo were not run.

The N16 closure record documents the accepted recovery: finish STA_STOP,
retire the stale lease/carrier, set the new runtime configuration and then
start STA. A blob-to-blob comparison against N16 commit `73218d9` found the
current Wi-Fi control implementation byte-identical apart from its relocated
path comment. This failure is therefore not caused by losing that N16 fix.

Before traffic, AP0/AP1 heartbeats were `2467/4970`, AP1 sleep entries were
`2467`, and AP1 mailbox wakes were `2491`. Afterwards these were
`6363/12806`, `6363` and `6409`, respectively. RPTUN remained CONNECTED and
the supervisor remained HEALTHY with zero fault, recovery and consecutive-
failure counters. The final non-halting SWD snapshot read AP SMP software
pending zero, PM peer-release pending zero, AON state zero and no pending
mailbox FIFO/interrupt state. AP PM diagnostics contained no error.

After the failed DHCP attempts, an additional non-halting SWD snapshot showed
both CP SDK-activity bitmaps zero, all AP fixed sleep/clock vote words zero,
AP SMP pending zero, peer-release pending zero, AON state zero and no pending
mailbox status. The Wi-Fi failure path therefore did not leak an activity vote
or strand AP1/PM state.

This RTT profile held the NuttX `system` standby wakelock for the full run, so
NuttX never requested coordinated standby and the CP PM diagnostic correctly
contained no entry attempt. This run proves retained-service traffic and AP1
liveness/coexistence; it is not low-voltage-entry evidence. The preceding
camera/H264 non-RTT run remains the direct proof of coordinated standby and
ordered AP1 release.

### Wi-Fi root-cause closure and retained debug profiles

Comparison with the official v3.1.1.9 SDK and Tuya wrapper path located the
failure below lwIP DHCP: the SDK Wi-Fi TX worker rejects queued transmission
when free heap is below its `10,240`-byte reserved threshold. NuttX's RTT
serial layer allocates both SEGGER transport buffers and serial RX/TX buffers
for each enabled channel. The previous RTT0/RTT1 sizing therefore drove the
observed minimum free heap to `7,088` bytes and made DHCP output return
`ERR_TIMEOUT(-3)` even though association and WPA had completed.

The permanent configuration keeps both debug channels and reduces only their
buffer sizes: RTT0 is `1024/128` bytes up/down and RTT1 is `1024/16` bytes
up/down. An alternate `cp_nsh_wifi_uart0_mcuboot` profile was also added so
UART0/COM3 can serve as a 115200-baud console while P0/P1 remains SWD. This is
an alternate build, not a replacement for J-Link or RTT. COM4 remains unused.

The alternate UART0 profile, version `18.6.7`, security counter `61`, was
sparsely flashed and directly verified. DHCP completed with status zero,
link state 3, address `192.168.0.100` and router `192.168.0.1`. The packet
diagnostic recorded positive heap reserve margin; minimum free heap was
`26,256` bytes. Coordinated PM continued with 2,001 attempts, 129 completed
entries and 129 wakeups. The download/session log is
`/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260813-113709`.

The final retained RTT profile, version `18.6.8`, security counter `62`, then
passed a clean full build and sparse COM3 update. All five executable writes
succeeded while LittleFS, `usr_config` and calibration remained untouched.
BL1 and BL2 were released through the existing P0/P1 J-Link hold. The download
log is
`/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260813-114819`.

The agent operated RTT directly through the no-GUI J-Link Telnet channel.
RTT0 exposed NSH and a single bounded `bkwifi connect 60000` completed with
status zero, link state 3, address `192.168.0.100`, mask `255.255.255.0` and
router `192.168.0.1`. Runtime credentials came from the host's saved WLAN
profile, were entered at the target's disabled-echo prompts and were neither
printed nor persisted.

Read-back of `g_bk7258_wifi_packet_diag` recorded three DHCP transmissions,
one Discover, two Requests, one Offer and one ACK. The last SDK TX result was
zero. Five heap samples observed a last free value of `22,968` bytes and a
minimum of `22,104` bytes, against the `10,240`-byte reserve; the last/minimum
margins were `12,728/11,864` bytes. This closes the earlier DHCP failure with
direct target evidence rather than repeated retries.

RTT1 was configured, selected by the J-Link Telnet channel, and the target
accepted `bkrpmsgtest syslog` with `BRPT SYSLOG PROBE SENT`. The host approval
service interrupted the subsequent RTT1-body read, so this record does not
claim that the final probe text was captured from channel 1. The channel's
build configuration and selectable control path are verified.

The final RTT profile retained NuttX's `system` wakelock and consequently had
zero CP coordinated-PM attempts, entries, aborts and wakeups. That is expected
for the retained debug profile and avoids conflating RTT liveness with a
low-voltage proof. The UART0 profile and the earlier camera profile remain the
real-board coordinated-entry evidence.
