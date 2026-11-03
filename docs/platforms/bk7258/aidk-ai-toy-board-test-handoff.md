<!-- SPDX-License-Identifier: Apache-2.0 -->

# AIDK AI Toy board-test evidence (2026-09-01)

Snapshot date: 2026-09-01
Repository: `contest2026_135_yongwangzhiqian`
Branch: `dev-ai-contest-2026`
Hardware-code HEAD: `e270b43` (`fix(bk7258): restore ICMP ping socket support`)

This is a dated board-evidence snapshot for generation 189, not a current status
or operating procedure. Current support and acceptance are recorded only in
`boards/bk7258/CONFIGS.md`; build, flash and trust steps follow the maintained
[build, package and hardware evidence SOP](nuttx-port/bk7258-build-flash-debug-sop.md).

## Hardware-fix commits

The hardware closure after `de3e9af` is:

| Commit | Purpose | Hardware evidence |
|---|---|---|
| `cafd26e` | RF provisioning and `sys_rf` preservation contract | accepted for the tested unit |
| `bf22d8d` | narrowly accept unsupported HCI Host Buffer Size | observed working on board |
| `520fd28` | defer attached-device discovery after RCS | observed working on board |
| `18ece71` | chip-level UART restore after coordinated standby | retained |
| `de3e9af` | stabilize interactive Wi-Fi input and validate PSKs | foreground input and bounded returns observed |
| `56a7ef6` | re-enter the chip UART lifecycle before the MFRC522 open | MFRC522 probe passed on generation 187 |
| `102c8b9` | honor SDK SRAM-only allocation and PSRAM free ownership | WPA handshake and DHCP passed on generation 188 |
| `e270b43` | select the AP ICMP socket dependency with BK7258 Wi-Fi vnet | connected status and gateway ping passed on generation 189 |

## Installed test artifact

The board reported:

```text
pair=confirmed version=18.6.129+189 counter=189
```

The signed, same-unit delivery was:

- product: `aidk_ai_toy-v18.6.129+189-icmp-ping.zip`;
- product SHA-256:
  `8eedd1b7bd4a1b5ba53d141e1fecb3b1a77a53c6bb1e8077596531a73356cc1a`;
- dense operator image SHA-256:
  `d324e4b37a5645ef11c01a523e79965f831bfae4362b817f445f1a24d0015b72`;
- full package SHA-256:
  `5739b35f749f5e7f96722ff86b57fe7c623625472e6dcff5215e5bb6db7e160a`;
- device: `C8:47:8C:CB:7F:80` only;
- generation: 189;
- OTA component: not included.

Package, public-trust and delivery verification passed. Independent comparisons
confirmed that `usr_config`, `persistent_data`, `easyflash`, `easyflash_ap`,
`sys_rf` and `sys_net` matched the accepted same-unit source. The 4 KiB
`sys_rf` digest remained
`b6cb961f09a3ad8c3c5d749a27c63dbb010015f67cb06c77ea37f211c8cee781`.
The clean build used fresh independent ephemeral P-256 BL1/MCUboot roots; the
temporary private-key directory was removed after package verification.

## Fresh board evidence

### MFRC522 UART probe passed

Generation 187 produced a configured UART snapshot and a valid device reply:

```text
AIDK MFRC522 UART SNAP stage=open-ready cfg=000a931b
AIDK MFRC522 UART probe: attempt=1 speed=eb/eb version=92 ret=0
AIDK MFRC522 registered: /dev/nfc0 via UART1
AIDK DEFERRED stage=mfrc522-pass
AIDK DEFERRED DONE failures=0
```

The board layer owns the UART1 binding and probe policy only. It requests
`bk7258_uart_runtime_reinitialize(1)` before opening the device; the chip layer
owns locking and the public SDK deinitialize/initialize lifecycle. The register
snapshot is bounded diagnostic evidence, not a board-owned UART implementation.
Relevant code did not change between generations 187 and 189.

### Wi-Fi association, DHCP and ping passed

Generation 189 reached every connection stage and returned a DHCP lease without
exposing the entered credential:

```text
scanu_confirm: status=0 upload_cnt=19 recv_cnt=63 result=1
State: SCANNING -> ASSOCIATING
State: ASSOCIATING -> ASSOCIATED
State: ASSOCIATED -> 4WAY_HANDSHAKE
State: 4WAY_HANDSHAKE -> GROUP_HANDSHAKE
State: GROUP_HANDSHAKE -> COMPLETED
sta:DHCP_ACK received
BKWIFI RESULT operation=connect status=0 link=3 rssi=-37 ip=192.168.0.102
BKWIFI RESULT operation=ping status=0 link=3 rssi=-37 ip=192.168.0.102
BKWIFI RESULT operation=status status=0 link=3 rssi=-37 ip=192.168.0.102
BKWIFI RESULT operation=ping status=0 link=3 rssi=-37 ip=192.168.0.102
```

Generation 188 had returned `status=-93` while retaining the link and lease.
Git history showed that the original ping implementation selected
`CONFIG_NET_ICMP_SOCKET`, while the later AIDK networking integration retained
ICMP but omitted the socket interface. `bk7258_wifi_ping_gateway()` uses a raw
`IPPROTO_ICMP` socket, so the INET dispatcher returned `-EPROTONOSUPPORT`.
Commit `e270b43` makes `BK7258_WIFI_VNET` select `NET_ICMP_SOCKET` on the AP
core. It changes neither SDK nor NuttX source. The clean generation-189 AP
configuration resolved both `CONFIG_NET_ICMP=y` and
`CONFIG_NET_ICMP_SOCKET=y`.

The `bkwifi ping 1000` invocation printed usage because its timeout was outside
the accepted range; the later default-timeout and 10-second pings both passed.
There was no allocator failure, assertion, panic, reboot, credential echo or
shell-input interleaving.

### Bluetooth passed; optional N12V target was absent

Generation 189 retained the compatibility state and passed ordinary discovery,
the combined suite, controller information and statistics:

```text
BBTT PASS operation=scan gen=1 sequence=1
BBTT SUITE PASS info=0 scan=1
BBTT PASS operation=info gen=1 sequence=4
BBTT SUITE PASS info=1 scan=0
BBTT PASS operation=scan gen=1 sequence=6
BBTT SUITE PASS info=1 scan=1
BBTT HCI command_tx=23 event_rx=629 invalid_rx=0 receive_errors=0
BBTT PASS operation=stats gen=1 sequence=7
```

The targeted `bkbttest n12v` invocation completed a valid scan but reported
`n12v_match=0` and suite status `-99` because no matching N12V advertisement
was present. This does not contradict the ordinary Bluetooth scan acceptance;
it is not evidence that an N12V peer was validated.

### Generation-189 topology remained healthy

After Wi-Fi traffic, the installed pair and supervisor remained healthy:

```text
pair=confirmed version=18.6.129+189 counter=189
ap state=2 error=0
cpu2 state=8 error=0 ready=1 online=00000003
rptun state=4 error=0
supervisor state=2 faults=0 recoveries=0
AP supervisor state=HEALTHY(2) reason=NONE(0)
Supervisor faults/recoveries/consecutive=0/0/0
```

## Remaining evidence boundary

An independent `bkwifi scan 15000` was not recaptured on generation 189.
Generation 188 passed standalone Wi-Fi scanning, and the generation-189 delta
only enables the AP ICMP socket. Boot latency remains accepted from generation
184, and the MFRC522 success log remains from generation 187. Report those
generation boundaries explicitly; do not relabel older evidence as generation
189.

## Acceptance matrix

| Area | Pass condition | State on 2026-09-01 |
|---|---|---|
| Boot latency | RCS near the 1.3-second baseline; SDIO/MMC remains deferred | passed on `18.6.124+184`; not recaptured on generation 189 |
| AP topology | AP READY, CPU2 online, RPTUN connected, supervisor healthy/faults 0 | generation-189 `bkota status` and post-traffic health passed |
| Bluetooth | compatibility information plus `BBTT PASS` | ordinary scan and combined suite passed on generation 189; N12V peer not found |
| Wi-Fi association | directed scan, WPA handshake and DHCP, no assertion or input race | passed on `18.6.129+189` |
| Wi-Fi reachability | connected status and gateway ping after successful connect | status and two pings passed on `18.6.129+189`; standalone scan not recaptured |
| MFRC522 | configured UART snapshot, valid response and `mfrc522-pass` | passed on generation 187; relevant code unchanged through generation 189 |
| RF/device data | same-unit `sys_rf` and device-unique tail preserved byte-for-byte | generation-189 package verified; prior runtime acceptance retained |
