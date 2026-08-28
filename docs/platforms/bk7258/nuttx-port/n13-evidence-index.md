# BK7258 N13 BLE GAP/GATT evidence index

> Snapshot: 2026-08-03
> Status: **`board-verified`**
> Profiles: `cp_nsh_ble_gatt + ap_smp_ble_gatt`
> Boundary: official NuttX, apps and Beken SDK sources/static libraries remain read-only

This index records only peer-observed or board-observed results. A locally
accepted packet, attempted notification, cached Windows object or Controller
connection is not treated as an air-link success without the matching Windows
and board evidence.

## 1. Frozen source and image baseline

| Item | Frozen value |
|---|---|
| Contest base / branch | `d4661fd5106ea8c95f4cb73405bb7b953e5d129e` / `feat/bk7258-ble-gatt` |
| NuttX / apps | `e02f581e235fc7b527d57ff62b668ce625d139ab` / `e81a73794786189f15e6c9fe9931ffddd561fd73` |
| SDK bundle | official support v3.1.1.9; legacy bundle retained as fallback |
| CP CRC / padded image | `208f4d5d...bfd2b` / `5036bacd...5e7d` |
| AP CRC / padded image | `4fd8a199...cc68` / `ffedd085...6be4` |
| Factory image | `eddca830ba44d3be2829f8670eeb7f44e842253aa15a448d9c8236856937749e` |
| BLE verifier JSON | `fbb3632489e619109feb29b967e2452a65a9c74ebcae3afede29116aa2184662` |
| RPTUN layout JSON | `244e4aac5c4bce9da89d3ec4ba73bb32b663917d457a2915009d65c2b9de5256` |
| Windows no-GUI CLI | `e31a156bb5b0b2b2015071b1f3903e2b89f771770d4a249036ebf3a6d7df8922` |

The final sparse flash used the bootloader, CP and AP segments only and
preserved LittleFS. The flash and boot transcript is under
`$WORKSPACE/logs/bk7258-auto-debug/20260803-073017/`; all segment writes were
verified and the boot reached `PASS_NSH`.

N12 latest/legacy rollback and the restored N13 build had already passed under
`$WORKSPACE/logs/bk7258-n13/n13-v-build-regression/20260803/`. The final N13
rebuild additionally passed the RPTUN layout checker and the source/ELF BLE
verifier, including the inbound ACL reference-ownership fail-closed check.

## 2. Functional and lifecycle gates

| Gate | Result | Authoritative evidence |
|---|---|---|
| Physical cold startup | PASS `3/3`, `cold_path=yes` | `n13-pacing50-rts/20260803-031501`, `n13-v-cold-repeat/round2/20260803-040716`, `round3/20260803-040755` |
| GAP, fixed service/handles, echo, notify | PASS, `100/100`, CRC/lost/duplicate all 0 | `n13-conn-ref-fix/final-session-1.json`, SHA-256 `dbffa38f...69ad` |
| Invalid writes and link recovery | PASS: length/magic/version/CRC `4/4` rejected; valid echo, notify and rediscovery then pass | `n13-conn-ref-fix/final-session-2-negative.json`, SHA-256 `65ca0da0...92af` |
| Post-negative board state | PASS: Host/HCI/N13 `2/2`, errors 0, `bt_conn.ref=0` | serial `df37b2ae...377b`, J-Link `865f4559...acf0` |
| Formal reconnect repeat | PASS `20/20`; every run uses uncached discovery, read/echo/notify/unsubscribe/disconnect/rediscovery | `n13-final-reconnect-20/`; 20 JSON manifest digest `d9e0cacb...4728`, log manifest digest `82fb038b...0444` |
| Post-repeat lifecycle | PASS: Host/HCI/N13 `22/22/22`, `state/error/queue=2/0/0`, `bt_conn.ref=0` | serial `2c713406...5ef`, J-Link `4597611b...b40` |

The 20-run manifest digest is the SHA-256 of the sorted per-file `sha256sum`
output. A machine assertion also checked all 20 JSON files for `status=passed`,
`discovery_cache=uncached`, rediscovery, echo match, exact `1/1` notification,
zero CRC/loss/duplicates and quiet unsubscribe.

## 3. Active coexistence and resource gates

### 3.1 BLE plus RPMsg full matrix

- BLE: uncached discovery and `100/100` notifications passed with zero
  CRC/loss/duplicates; JSON SHA-256 `aa13ac79...ed49`.
- RPMsg: payload `1/64/464`, idle/load, six runs total; both logical CPUs were
  `100/100`, errors 0, suite PASS, and AP heap returned to its stable value;
  serial SHA-256 `4c342b02...499`.
- The bounded host process used a 90 s per-operation deadline and completed in
  45.41 s; time evidence SHA-256 `6db9fccc...9887`.

An earlier 30 s burst deadline timed out during the same full RPMsg workload.
The board remained healthy and re-advertised; that run is retained under
`n13-active-coexistence/rpmsg/` as a performance observation, not counted as a
PASS. The 90 s rerun proves bounded functional coexistence, while 45.41 s is
the initial contention baseline rather than a product latency SLA.

### 3.2 BLE plus RPMsgFS full matrix

- BLE: uncached discovery and `100/100` notifications passed with zero
  CRC/loss/duplicates in 14.18 s; JSON SHA-256 `1359067f...c89`, time evidence
  `03721356...9873`.
- RPMsgFS: payload `1/64/464/1024`, each `20/20`; byte counts and checksums all
  matched, suite PASS, and AP/CP heap stabilized after the first fixed service
  allocation; serial SHA-256 `19500381...aba`.

### 3.3 Final system state

After both active tests:

- AP `READY`, RPTUN `CONNECTED`, pending `0/0`, supervisor `HEALTHY`, CPU2
  `SCHEDULER_ONLINE`, all SMP gates PASS, no supervisor fault or recovery;
  SHA-256 `b7b19be3...1b2`.
- Host/HCI/N13 lifecycle is exactly `25/25/25`; HCI/Host send and receive
  errors, PDU failure and queue overflow are all 0; SHA-256
  `ddee8544...1bab`.
- J-Link read of `g_conns` shows `ref=0` at offset `+0x48` and
  `state=DISCONNECTED` at `+0x4c`; SHA-256 `74e1def6...c104`.

## 4. Board-only compatibility findings

The old image accumulated exactly 19 `bt_conn` references after one full
session, equal to `HOST conn_rx=19`. Source tracing showed that stock
`hci_acl()` obtains a reference through `bt_conn_lookup_handle()`, while the
current `hci_acl()`/`bt_conn_receive()` path does not release it. That prevents
the single connection slot from being reused even though the Controller can
connect again.

The final image keeps NuttX untouched. A board link wrapper calls the real
`bt_conn_receive()` and then releases exactly that caller-owned reference.
The verifier directly inspects the sibling official NuttX sources and fails if
their ownership pattern changes, preventing a future double release.

The Controller also auto-stops legacy advertising on connection while the
stock Host flag remains enabled. The board connect worker synchronizes that
flag; only at this proven post-connection point it accepts the generic `-EIO`
mapping returned for an already-stopped Controller. Disconnect then performs
one board-owned full advertising restart. Final lifecycle equality and RF
rediscovery are the functional proof.

## 5. Closure decision and boundaries

N13 is `board-verified` for the approved first-release scope: one Central,
connectable legacy advertising, GAP name, one static custom GATT service,
20-byte read/write-with-response, CCC notification, repeated reconnect and
coexistence with AP SMP/RPTUN/RPMsg/RPMsgFS/supervision.

The result does not claim indication, write without response, long/prepare
write, pairing/bonding, multiple Centrals, BLE Mesh, Wi-Fi/BLE coexistence or
Bluetooth AP-only warm restart. `BLEDebug.EXE` was never used and remains
forbidden by user policy. Official NuttX/apps tracked diffs are empty; official
SDK sources and static libraries were neither modified nor rebuilt.
