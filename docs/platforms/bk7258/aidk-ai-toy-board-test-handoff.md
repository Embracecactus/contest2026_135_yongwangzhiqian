<!-- SPDX-License-Identifier: Apache-2.0 -->

# AIDK AI Toy board-test handoff

Snapshot date: 2026-09-01
Repository: `contest2026_135_yongwangzhiqian`
Branch: `dev-ai-contest-2026`
Hardware-code HEAD: `102c8b9` (`fix(bk7258): honor SDK SRAM allocation contract`)

This is a live hardware-debug handoff, not an immutable verification record.
Begin the next session with `current mode: hardware-fast iteration`, read the
repository `AGENTS.md`, and replace this snapshot with new board evidence when
the target state changes.

## Scope boundary

The next session owns only the AIDK AI Toy board-test loop:

- finish post-connect reachability and the remaining health checks on
  generation 188;
- reconfirm the already-successful MFRC522 probe if the board is rebooted;
- preserve AP/RPTUN/CPU2, Bluetooth and deferred-bring-up behavior;
- build or hand off a directly flashable artifact only when the owner requests
  it and the applicable trust/device-data gates pass.

Voice-model training, private voice artifacts and OpenAI feedback belong to
other sessions. Do not read, stage, move or commit them during board debugging.

## Git and artifact boundary

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

Unrelated owner/parallel work was still present at handoff creation:

- modified `AGENTS.md`;
- untracked `tests/host/bk7258/test_wechat_voice_manifest.py`;
- untracked `tools/bkvoice/`.

Those paths were not staged or committed. Re-run
`git status --short --branch` before every edit or commit and stage only the
board-test dependency closure.

## Installed test artifact

The board reported:

```text
pair=confirmed version=18.6.128+188 counter=188
```

The signed, same-unit delivery was:

- product: `aidk_ai_toy-v18.6.128+188-wifi-sram-tx.zip`;
- product SHA-256:
  `63d507c6dd983d50fe1fba0a129651589d07570f2c64e48b33e9a654577243eb`;
- dense recovery SHA-256:
  `2a8ac6d4f13d9baf7dfda06990e434a4a26588d97ef7d1b0a94f89d625e75070`;
- device: `C8:47:8C:CB:7F:80` only;
- generation: 188;
- OTA component: not included.

Package, public trust and delivery verification passed. Independent comparisons
confirmed that `usr_config`, the 1 MiB `persistent_data` window, the complete
device-unique tail and `sys_rf` matched the accepted same-unit base. The 4 KiB
`sys_rf` digest remained
`b6cb961f09a3ad8c3c5d749a27c63dbb010015f67cb06c77ea37f211c8cee781`.

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

The board layer still owns only the UART1 device binding and probe policy. It
requests `bk7258_uart_runtime_reinitialize(1)` before opening the device; the
chip layer owns locking and the public SDK deinitialize/initialize lifecycle.
The register snapshot is bounded diagnostic evidence, not a board-owned UART
implementation.

### Wi-Fi association and DHCP passed

On generation 188, an initial attempt returned an ordinary timeout because its
directed scan matched no SSID. A retry with the intended SSID reached every
required connection stage:

```text
scanu_confirm: status=0 upload_cnt=22 recv_cnt=48 result=1
State: SCANNING -> ASSOCIATING
State: ASSOCIATING -> ASSOCIATED
State: ASSOCIATED -> 4WAY_HANDSHAKE
State: 4WAY_HANDSHAKE -> GROUP_HANDSHAKE
State: GROUP_HANDSHAKE -> COMPLETED
sta:DHCP_ACK received
BKWIFI RESULT operation=connect status=0 link=3 rssi=-40
```

No `__l2_packet_send: send timeout`, allocator failure, PSRAM rejection,
assertion, panic, credential echo or shell-input interleaving occurred. The
fix keeps the 96 KiB CP PSRAM system-heap extension but reserves a separate
16 KiB internal-SRAM heap for the SDK's SRAM-only allocation API. The CP RAM
trace buffer was reduced from 32 KiB to 16 KiB, leaving MCUboot CP BSS within
16 bytes of the previous generation.

The same generation-188 boot also established, before the successful retry:

- standalone scan passed with 20 results;
- `bkbttest scan` ended with `BBTT PASS` and `BBTT SUITE PASS`;
- AP was `READY`, CPU2 was `SCHEDULER_ONLINE`, RPTUN was `CONNECTED`;
- the supervisor was `HEALTHY` with zero faults and zero recoveries.

### Post-connect ping returned a bounded error

After the successful connection, the ping command retained the connected link,
RSSI and DHCP lease but returned a nonzero status:

```text
BKWIFI RESULT operation=ping status=-93 link=3 rssi=-40 ip=192.168.0.102 mask=255.255.255.0 router=192.168.0.1
AP supervisor state=HEALTHY(2) reason=NONE(0)
Supervisor faults/recoveries/consecutive=0/0/0
```

The command returned to NSH without an assertion, panic or reboot, and the
post-connect supervisor health check passed. This is useful bounded failure
evidence, but `status=-93` is not proof of network reachability and still needs
diagnosis.

Do not record the real SSID or password in this document or in captured logs.

## Remaining hardware checks

The successful connection returned a DHCP lease, and post-connect
`apctl health` passed. The following commands were not recaptured after that
successful attempt:

```text
bkwifi status
bkwifi scan 15000
bkbttest scan
apctl status
bkota status
```

Acceptance requires status to remain connected, scan to return normally,
Bluetooth to retain `BBTT PASS`, and the full AP/CPU2/RPTUN topology to stay
healthy after Wi-Fi traffic. Diagnose the bounded ping `status=-93` and obtain
positive reachability evidence before declaring ping passed. An ordinary
network error is useful evidence, but must not be reported as reachability
success.

If the board is rebooted, also retain the bounded MFRC522 `open-ready`, probe
result and `mfrc522-pass` lines. Do not reopen the UART repair solely because a
new boot log was not captured; reopen it only on contradictory runtime evidence.

## Acceptance matrix

| Area | Pass condition | Current handoff state |
|---|---|---|
| Boot latency | RCS near the 1.3-second baseline; SDIO/MMC remains deferred | passed on `18.6.124+184`; not recaptured on generation 188 |
| AP topology | AP READY, CPU2 online, RPTUN connected, supervisor healthy/faults 0 | full status passed before the final retry; supervisor health passed after connect |
| Bluetooth | compatibility information plus `BBTT PASS` | passed on generation 188 before the final Wi-Fi retry |
| Wi-Fi association | directed scan, WPA handshake and DHCP, no assertion or input race | passed on `18.6.128+188` |
| Wi-Fi reachability | connected status, ping and scan after successful connect | ping retained link/IP but returned `-93`; open |
| MFRC522 | configured UART snapshot, valid response and `mfrc522-pass` | passed on generation 187; unchanged in generation 188 |
| RF/device data | same-unit `sys_rf` and device-unique tail preserved byte-for-byte | package verified; prior runtime acceptance retained |

## Flash and trust guardrails

- Do not flash merely because this handoff exists. Wait for the owner's explicit
  request and identify the exact target artifact first.
- Do not use J-Link in the AIDK board-test loop.
- Prefer an installed apps-only path only when the installed public trust root
  accepts the CP/AP change.
- Every authorized MCUboot whole-device download starts a clean build and a new
  pair of independent ephemeral P-256 BL1/MCUboot keys with a strictly
  increasing rollback generation. Never reuse old private keys.
- A dense 8 MiB image must materialize `sys_rf` and the device-unique tail from
  the accepted complete readback of this same unit. Never copy it to another
  board and never chip-erase the target.
- A universal factory image remains unavailable until per-unit
  RF/MAC/Bluetooth provisioning exists.

Use the maintained
[build, package and hardware evidence SOP](nuttx-port/bk7258-build-flash-debug-sop.md)
for any authorized build or package operation.

## Copy-ready prompt for the next session

```text
接管 AIDK AI Toy 板上快速调试。先完整阅读仓库 AGENTS.md 和
docs/platforms/bk7258/aidk-ai-toy-board-test-handoff.md，并声明
current mode: hardware-fast iteration。只处理板测，不接管语音训练或 OpenAI
反馈。当前板上是已确认的 18.6.128+188/counter 188；MFRC522 已在 generation
187 探测成功，Wi-Fi 已在 generation 188 完成 WPA 握手和 DHCP；联网后 ping
保留 link=3 和 DHCP 地址但返回 status=-93，supervisor health 仍通过。先诊断
ping 的有界错误并补采 status/scan，再复核 Bluetooth 和完整 AP/CPU2/RPTUN
状态。
不要使用 J-Link，不要泄漏 Wi-Fi 凭据，不要碰 sys_rf，也不要执行未授权全量
烧录。只有新板证据与当前记录冲突时才重新打开已经闭环的驱动假设。
```
