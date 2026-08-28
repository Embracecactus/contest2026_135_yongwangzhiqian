# BK7258 Stage N16: Wi-Fi STA control and NuttX data plane

> Date: 2026-08-05
> Status: **COMPLETE / STA data plane, retained-service coexistence and bounded lifecycle matrix board-verified**
> SDK: official Beken v3.1.1.9 only
> Decision: [ADR-007](../../../../../memory/decisions/ADR-007-n16-cp-radio-ap-nuttx-network.md)

## 1. Goal

Bring up a production-shaped Wi-Fi STA path without modifying official
NuttX, apps, SDK source or SDK static libraries:

1. CP owns the official RF/PHY/MAC/WPA/controller path;
2. AP logical CPU0 owns the official Wi-Fi proxy and mailbox gateway;
3. a board-owned adapter exposes `wlan0` to the native NuttX network stack;
4. the official CP vnet controller performs DHCP, AP synchronizes that lease
   into `wlan0`, and native NuttX provides IPv4/TCP/UDP/socket APIs;
5. Wi-Fi traffic coexists with AP SMP, RPTUN/RPMsg, RPMsgFS and Bluetooth.

N16 is not network OTA. N15 remains the update/rollback baseline; signatures,
anti-rollback and network delivery are later independent stages.

## 2. Accepted architecture

```text
                         BK7258

  CP NuttX / physical CPU0
  +----------------------------------------------------+
  | shared radio platform init                         |
  | official RF / PHY / MAC / WPA supplicant           |
  | official Wi-Fi vnet controller (cif) + DHCP client |
  +-------------------+--------------------------------+
                      | MB_CHNL_WIFI_CMD  (index 4)
                      | MB_CHNL_WIFI_DATA (index 5)
                      | commands + shared pbuf pointers
                      v
  AP NuttX SMP / physical CPU1 + CPU2
  +----------------------------------------------------+
  | logical CPU0: official wdrv + team pbuf/netdev gate |
  |                     |                              |
  |                     v                              |
  |          lease sync -> NuttX wlan0                 |
  |           IPv4 / DHCP / TCP / UDP / sockets        |
  |                     |                              |
  | logical CPU0/1: application socket users            |
  +----------------------------------------------------+
```

The official AP lwIP stack is not linked. The official vendor command/data
protocol remains intact; only its AP network-interface boundary is replaced.
The exact v3.1.1.9 controller performs DHCP on CP and reports its lease through
the command channel. AP does not start a competing DHCP client: it applies the
reported address, mask and router to native NuttX `wlan0`. NuttX remains the
sole AP IPv4/TCP/UDP/socket implementation.

## 3. Source-verified baseline

All facts in this section are from the exact v3.1.1.9 source tree and bundled
archives. They are not assumptions from a newer or legacy SDK.

### 3.1 Official role split

- `projects/wifi/iperf/cp/config/bk7258/config` enables
  `CONFIG_WIFI_VNET_CONTROLLER=y`.
- `projects/wifi/iperf/ap/config/bk7258_ap/config` enables
  `CONFIG_BK_NETIF=y` and `CONFIG_NETIF_LWIP=y`.
- CP `components/bk_wifi/src/wifi_init.c` starts workqueue, MAC/PHY,
  calibration, Wi-Fi threads, WPA and `cif_init()`.
- AP `components/bk_wifi/src/wifi_api.c` starts `wdrv_init()` and creates the
  host network interface.
- `MB_CHNL_BT_CMD`, `MB_CHNL_WIFI_CMD`, `MB_CHNL_WIFI_DATA` and
  `MB_CHNL_LOG` use logical indices 3, 4, 5 and 14 respectively. They do not
  collide, but Wi-Fi has priority over the project RPTUN channel.

### 3.2 Archive boundary

The relevant official archives are:

| Role | Required candidate archives | Deliberately excluded |
|---|---|---|
| CP | `libwifi.a`, `libbk_wifi.a`, `libwpa_supplicant-2.10.a`, `libcontroller_if.a`, PHY/RF/coexistence closure | SDK top-level application/startup |
| AP | `libbk_wifi_driver.a`, selected `libbk_wifi.a` objects | vendor `liblwip_intf_v2_1.a`, SDK FreeRTOS implementation |

Object-level `nm` evidence already shows the intended AP seam:

- `wdrv_main.c.obj` needs `rtos_smp_create_thread()` and pbuf helpers;
- `wdrv_rx.c.obj` delivers frames through `ethernetif_input()`;
- `wifi_api.c.obj` calls `wdrv_init()` plus a bounded set of host-netif/IP
  helpers;
- those functions do not by themselves require the complete vendor socket
  stack.

This is promising but not yet the N16-R exit gate. The exact pulled-object
closure and final ELF ownership must still be proven.

### 3.3 Existing project interactions

- The N12 Bluetooth wrapper already initializes flash, ADC, PHY/RF and
  calibration before starting the controller. N16 must reuse or upgrade that
  state instead of running a second platform initialization sequence.
- Bluetooth-only mode currently publishes a minimal PHY callback table through
  `g_wifi_funcs`. Wi-Fi mode needs the exact full official callback table;
  replacement order and coexistence must be verified before controller start.
- `bk7258_os_adapt.c` implements `rtos_create_thread()` but not
  `rtos_smp_create_thread()`. N16 must provide the missing board wrapper and
  pin its task to AP logical CPU0; linking `libbk_rtos.a` is forbidden.
- Current Wi-Fi-disabled profiles must remain byte/link-behavior compatible
  apart from explicitly documented build nondeterminism.

### 3.4 Wi-Fi allocation compatibility boundary

Board A/B evidence established that the immutable v3.1.1.9 CP Wi-Fi archive
assumes selected `malloc()` results are initially zero.  In particular,
`me_strategy_mem_init()` allocates the station table without fully clearing
it, and `sta_mgmt_entry_init()` consumes an embedded list head as prior state.
This happens to work in the official early-start environment because Wi-Fi
sees a fresh zero-filled FreeRTOS heap; it is not a contract guaranteed by
standard `malloc()` or by NuttX after other initialization has reused heap
blocks.

The permanent compatibility policy is deliberately component-scoped:

1. the CP Wi-Fi profile links `--wrap=malloc` because the direct calls are
   inside immutable archives;
2. `bk7258_os_wifi_malloc_zero_begin()` records the PID that executes
   `bk_wifi_init()` and then publishes the active flag;
3. `__wrap_malloc()` clears a result only when both the flag is active and the
   current PID equals that Wi-Fi owner PID;
4. `bk7258_os_wifi_malloc_zero_end()` disables the scope and removes the PID;
5. allocations from every other CP thread keep normal NuttX semantics, even
   while `bk_wifi_init()` is running.  AP and Wi-Fi-disabled images do not
   acquire this behavior.

Do not replace this with a return-address whitelist: SDK internal addresses
are not a stable ABI.  The wrapper may be removed only when the selected
official SDK initializes every affected object itself, or when Wi-Fi moves to
a verified dedicated allocator whose zero-initialization is explicit.

For later components that show pointer/list faults around first initialization,
do not broaden this Wi-Fi scope.  Temporarily observe that component's own
allocation window, record caller, owner PID, size and nonzero-byte evidence,
symbolize the first invalid PC/LR, and then choose a separately named
component scope only if the same hidden initialization contract is proven.
Restore all observation-only instrumentation after the diagnosis.

Canonical evidence:
[N16 Wi-Fi malloc compatibility verification](../../../../../progress/verification/2026-08-05-n16-wifi-malloc-compatibility.md).

## 4. Shared-buffer and thread contract

The official vnet protocol sends shared SRAM pointers, not RPMsg payloads.
Both processors can address the CP and AP SRAM windows, but addressability is
not ownership.

N16-R must freeze and test:

1. exact 32-bit `struct pbuf` size, offsets, flag/reference widths and
   alignment from the v3.1.1.9 headers;
2. the active `CONFIG_MSDU_RESV_HEAD_LENGTH=108` and
   `CONFIG_MSDU_RESV_DESC_LENGTH=600` headroom contract;
3. TX allocation/ref/free ownership through completion;
4. RX copy/completion/free ordering used by
   `CONFIG_CONTROLLER_RX_DIRECT_PSH=1`;
5. range checks proving that every cross-core pointer belongs to an approved
   CP/AP SRAM region;
6. current cache-off/non-cacheable assumptions plus explicit memory barriers.

Initial policy:

- one AP logical CPU0 vendor worker owns command and data callbacks;
- mailbox ISR callbacks only enqueue bounded work;
- NuttX network ingress happens in a safe CPU0 worker context under the
  required network lock, never by blocking in the ISR;
- no new reserved carveout is introduced unless the exact allocation model
  proves it necessary;
- RX may copy once at the vendor-to-NuttX seam. Zero-copy is an optimization,
  not a first-release requirement.

## 5. Lifecycle contract

Cold startup order:

```text
CP board/platform init
  -> shared PHY/RF/calibration init exactly once
  -> Bluetooth controller init when configured
  -> official Wi-Fi controller + CIF init when configured
  -> release/start AP
  -> AP Wi-Fi proxy worker on logical CPU0
  -> register wlan0
  -> scan / associate
  -> NuttX DHCP
```

Official v3.1.1.9 `wifi_deinit()` returns `BK_ERR_NOT_SUPPORT`; AP
`wdrv_deinit()` removes its task/queue but does not close mailbox channels.
Therefore the initial N16 policy is fail-closed:

- AP-only stop/restart/cycle is rejected with `-EBUSY` while Wi-Fi is active;
- no reconnect or stale-pointer recovery is claimed;
- whole-chip reset is the recovery boundary for a Wi-Fi controller/transport
  fault;
- Wi-Fi shutdown/warm restart is a separate later design task.

## 6. Implementation phases

### N16-R: source, ABI and link closure

Exit gates:

- [ ] record the exact v3.1.1.9 source/archive provenance and archive hashes;
- [ ] generate an object-level defined/undefined dependency report for the CP
      controller and AP proxy entry points;
- [ ] compile an ABI probe with static assertions for pbuf/cpdu layout,
      alignment and headroom;
- [ ] link a minimal AP seam proving no vendor lwIP socket, FreeRTOS scheduler
      or duplicate NuttX network symbol is pulled;
- [ ] verify every mailbox callback context, ownership transition and memory
      barrier from source;
- [ ] audit Wi-Fi logs so SSID/password output is disabled or redacted;
- [ ] define positive and deliberately broken negative fixtures for each
      verifier rather than accepting grep-only evidence.

No board write is needed for N16-R.

### N16-A: CP controller and shared radio platform

- add Wi-Fi-specific Kconfig/link gates, disabled in existing profiles;
- refactor the current Bluetooth PHY/RF/calibration bootstrap into an
  idempotent shared radio-platform wrapper;
- install the full official Wi-Fi callback table before any consumer can
  dereference it;
- initialize official CP event/netif/controller/CIF leaves in the verified
  order;
- preserve Bluetooth-only and Wi-Fi-only builds as separate negative/positive
  closure tests before enabling coexistence.

Exit: exact-v3.1.1.9 CP image builds and its final ELF contains the intended
controller objects/channels without SDK startup or lwIP application closure.

### N16-B: AP command plane

- add `rtos_smp_create_thread()` in the repository OS adapter and pin the
  vendor worker to AP logical CPU0;
- bring up official `wdrv` command IPC and the selected Wi-Fi API proxy;
- provide bounded team-owned event/netif compatibility leaves;
- expose scan, set-config, start, stop and status through a test-only
  `bkwifitest` command;
- redact credentials and clear temporary password buffers after use.

Exit: command channels initialize and host/link tests cover scan/connect event
translation; no claim of an IP data plane yet.

### N16-C: pbuf compatibility and NuttX `wlan0`

- implement only the exact pbuf operations required by pulled vendor objects;
- implement TX conversion from NuttX poll output into the vendor pbuf/headroom
  layout and preserve completion ownership;
- implement `ethernetif_input()` as the reviewed vendor RX seam, validate the
  frame and inject it into a registered NuttX Ethernet netdev;
- bound queues and return `-EAGAIN`/drop counters under pressure rather than
  blocking the vendor worker indefinitely;
- add compile, host-unit and final-ELF checks for ownership and forbidden
  symbols.

Exit: synthetic L2 TX/RX fixtures pass and `wlan0` is registered in the AP
image.

### N16-D: STA, DHCP and native sockets

- create dedicated `cp_nsh_wifi + ap_smp_wifi` validation profiles;
- enable the minimal NuttX IPv4, ARP, ICMP, TCP and UDP features;
- synchronize the CP vnet controller's DHCP lease into AP `wlan0`; do not run
  a second AP DHCP client;
- accept test SSID/password only at runtime; do not place credentials in
  defconfig, repository logs or project memory;
- verify scan -> association -> link-up -> DHCP lease -> gateway ping;
- exchange TCP and UDP data with a local, operator-controlled endpoint so the
  gate does not depend on public Internet availability.

Exit: NuttX applications use normal sockets over `wlan0`; the vendor lwIP
socket layer remains absent from the ELF.

Board status: complete for the agreed STA matrix. Runtime-only association,
bounded wrong-password failure followed by correct-password recovery, lease
synchronization, gateway ICMP, and local TCP/UDP exchange all passed.

### N16-E: SMP, service coexistence and failure behavior

- run Wi-Fi traffic while AP logical CPU1 is an active socket producer;
- run retained RPTUN/RPMsg and RPMsgFS traffic under Wi-Fi mailbox load;
- run Bluetooth control/GATT traffic under Wi-Fi load without using
  `BLEDebug.EXE`;
- measure mailbox errors, queue high-water marks, drops, RTT, throughput and
  CPU0 utilization as a baseline;
- prove AP-only restart is rejected while Wi-Fi is active and a whole-chip
  reset returns to a clean state.

Exit: no stale pointer, deadlock, unbounded wait, heap drift or retained-service
failure in the agreed finite matrix.

Board status: the bounded retained-service matrix passed while Wi-Fi remained
associated. `bkrpmsgfstest` initially exposed reuse of a live VFS file object
by a nested small allocation in the shared AP main heap. The permanent,
NuttX-supported boundary is `CONFIG_FS_HEAPSIZE=16384` in `ap_smp_wifi`, which
places VFS/RPMsgFS/socket metadata in the independent FS heap. The final clean
image passed all 1/64/464/1024-byte RPMsgFS cases with unchanged AP/CP heap
measurements, plus the two-CPU RPMsg, Bluetooth-info and AP/RPTUN/SMP health
gates under an active Wi-Fi link. No official NuttX source was changed.

Canonical evidence:
[N16 STA and coexistence board verification](../../../../../progress/verification/2026-08-06-n16-wifi-sta-coexistence.md).

### N16-V: board closure

Minimum board evidence:

| Gate | Required result |
|---|---|
| boot | CP/AP READY, AP logical CPU1 online, RPTUN connected |
| scan | target AP found; no credential printed |
| security | WPA2 association succeeds; wrong password fails boundedly |
| network | NuttX DHCP lease, ARP and gateway ICMP pass |
| data | local TCP and UDP echo pass with byte/count validation |
| SMP | socket producers on AP logical CPU0 and CPU1 both pass |
| coexistence | active Wi-Fi + RPMsg + RPMsgFS + Bluetooth finite suites pass |
| lifecycle | 3/3 controlled RTS resets return cleanly; complete power removal remains separately labelled |
| regression | N15 layout, LittleFS, PSRAM and OTA gates-zero normal baseline remain intact |

Board result: complete for the agreed finite scope.

- `apctl restart 3000` returned `-EBUSY` while Wi-Fi was active, before any AP
  reset action. AP generation remained unchanged, AP/RPTUN/SMP stayed healthy,
  and Wi-Fi status plus gateway ICMP still passed.
- Three consecutive COM7 RTS resets each reached NSH with AP `READY`, RPTUN
  `CONNECTED`, CPU2 `SCHEDULER_ONLINE`, SMP gates `PASSED` and supervisor
  fault count zero. Runtime credentials were absent after reset as designed;
  one final hidden-input reconnect and gateway ICMP passed.
- Final host checks passed dynamic partition generation, factory boundaries
  and all six Tier-1 Boot OTA gates at zero.
- A post-reset 64-byte RPMsgFS write/read passed against CP LittleFS. The Wi-Fi
  profiles intentionally do not enable the separate N14 PSRAM validation
  profile, so N16 does not claim a new PSRAM run; the previously verified N14/
  N15 PSRAM baseline and its profile boundary are unchanged.

Operator-provided SSID/password and local endpoint values were ephemeral inputs
and were not committed.

## 7. Planned repository organization

The exact filenames may be refined during N16-R, but ownership remains:

```text
board/bk7258/
  chip/cp/       shared radio init + CP Wi-Fi controller wrapper
  chip/ap/       AP Wi-Fi control + pbuf/netdev adapter
  chip/common/   only genuinely role-shared OS/ABI helpers
  configs/       cp_nsh_wifi and ap_smp_wifi validation profiles
  scripts/       source/ABI/link/layout verifiers
  tests/         portable positive and negative fixtures

docs/platforms/bk7258/nuttx-port/
  prompts/16-n16-wifi-data-plane.md
  n16-wifi-source-verification.md        # created when N16-R evidence closes

progress/verification/
  YYYY-MM-DD-n16-*.md                    # material host/board evidence only
```

No file is added under official NuttX, apps or SDK source trees.

## 8. Explicit exclusions

- legacy SDK build or compatibility validation;
- vendor lwIP/socket stack in the AP NuttX image;
- CP-hosted remote socket service over RPMsg;
- SoftAP, bridge/repeater, monitor mode, raw injection or WPA3 claims;
- Wi-Fi power-save/deep-sleep tuning;
- Wi-Fi warm restart or AP-only recovery;
- network OTA transport, signed update, key provisioning and anti-rollback;
- upper-8 MiB general PSRAM allocator work;
- QEMU or unrelated hardware-debug SOP changes.

## 9. Completion and next stage boundary

N16 is complete for the accepted STA scope. The implementation changes remain
to be committed and published. N17 authenticated-update policy is the proposed
next MAIN Stage, but it requires a separate architecture review before code.
SoftAP, warm Wi-Fi teardown/recovery, performance tuning and network OTA remain
outside N16.
