# ADR-007: Keep Wi-Fi radio control on CP and use the NuttX network stack on AP

- Status: Accepted; implementation pending N16-R ABI/link closure
- Date: 2026-08-04
- Decision owner: Project owner

## Context

BK7258 has one CP core and an AP SMP cluster. The official Beken v3.1.1.9
`projects/wifi/iperf` project does not place the complete network stack on CP:

- CP enables `CONFIG_WIFI_VNET_CONTROLLER`, owns RF/PHY/MAC, WPA and the
  controller side of the vendor virtual-network link;
- AP enables `CONFIG_BK_NETIF` and `CONFIG_NETIF_LWIP`, owns the vendor Wi-Fi
  proxy, IP stack and applications;
- CP and AP exchange commands and Ethernet-frame buffers on
  `MB_CHNL_WIFI_CMD` and `MB_CHNL_WIFI_DATA`.

This project already runs NuttX on both sides. AP NuttX owns the SMP business
tasks and native POSIX socket surface. Linking the complete vendor AP lwIP
stack would create a second IP/socket implementation in the same image.

## Decision

Preserve the official v3.1.1.9 hardware and IPC ownership while replacing the
official AP lwIP boundary with a repository-owned NuttX netdev adapter:

```text
CP NuttX / CPU0
  official RF + PHY + MAC + WPA + Wi-Fi controller
             |
             | official WIFI_CMD / WIFI_DATA mailbox ABI
             v
AP NuttX SMP / logical CPU0 gateway
  official AP Wi-Fi proxy/driver
  repository-owned pbuf compatibility + wlan0 netdev adapter
             |
             v
  native NuttX IPv4 / DHCP / TCP / UDP / sockets
```

The official NuttX/apps/SDK sources and SDK archives remain immutable.
Permanent integration belongs in board-owned Kconfig, link selection, OS
adapters, Wi-Fi wrappers, tests and documentation.

The initial implementation is STA-only. AP logical CPU0 owns the Wi-Fi
mailbox callbacks, vendor worker, pbuf compatibility boundary and NuttX
netdev ingress/egress. Logical CPU1 may run applications and use sockets but
does not directly own the vendor transport.

## Rejected alternatives

1. **Link the official AP lwIP stack unchanged.** Rejected because NuttX
   already supplies the product IP/socket stack; duplicate netif, socket and
   libc-facing symbols would create ambiguous ownership and larger ABI risk.
2. **Put IP/TCP/UDP on CP and expose remote sockets over RPMsg.** Deferred
   because it departs from the official SMP data path, adds a new remote
   socket protocol and makes lifecycle, zero-copy, backpressure and error
   translation substantially more complex.
3. **Replace the official Wi-Fi mailbox protocol with RPMsg Ethernet.**
   Rejected for the first release because the immutable Wi-Fi archives
   already implement pointer ownership and completion on their dedicated
   vendor channels.

## Consequences and constraints

- N16-R must prove the exact v3.1.1.9 archive closure and vendor `pbuf` ABI
  before production code links the Wi-Fi libraries.
- The final AP ELF must not pull the vendor lwIP socket stack or SDK FreeRTOS
  implementation. A project wrapper will provide `rtos_smp_create_thread()`
  and pin the vendor worker to AP logical CPU0.
- CP Bluetooth and Wi-Fi share PHY/RF/calibration state. Their initialization
  must be one idempotent repository-owned platform sequence; double
  initialization is forbidden.
- Wi-Fi command/data logical mailbox channels 4/5 do not collide with
  Bluetooth channel 3 or RPTUN channel 14, but their higher priority can
  affect fairness. Wi-Fi + Bluetooth + RPTUN/RPMsgFS coexistence is a required
  board gate.
- Official v3.1.1.9 `wifi_deinit()` is unsupported and the AP driver does not
  close its mailbox channels. AP-only restart while Wi-Fi is active must fail
  closed; the initial recovery boundary is a whole-chip reset.
- SSIDs and passwords must never be compiled into a defconfig, persisted in
  project memory, or emitted unredacted to logs.

## Acceptance gates

1. Exact archive/object closure and `pbuf` layout are source/compile/link
   verified against official v3.1.1.9.
2. Wi-Fi-disabled normal profiles retain their current link closure and
   behavior.
3. A dedicated profile scans, associates as STA, obtains a NuttX DHCP lease
   and exchanges IPv4 data through `wlan0`.
4. AP logical CPU0 remains the sole transport gateway while applications on
   both AP logical CPUs can use the NuttX socket API.
5. Active Wi-Fi traffic does not break the retained SMP, RPTUN/RPMsg,
   RPMsgFS or Bluetooth baselines.

## Reversal signals

- Exact v3.1.1.9 object-level evidence shows that the AP driver cannot be
  separated from the vendor lwIP socket implementation.
- The vendor pointer/pbuf ABI cannot be represented safely without changing
  official source or allowing unbounded shared-pointer ownership.
- Board evidence shows mailbox priority starvation that cannot be bounded in
  repository-owned wrappers.
