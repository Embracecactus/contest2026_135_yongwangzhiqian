# N15-F validation profile and volatile transport verification

- Verified: 2026-08-04T05:46:20+08:00
- Scope: health policy, validation-only command path, volatile PSRAM transport,
  exact-source contract, ARM ELF and complete dual build
- SDK: official Beken v3.1.1.9 only
- Board/J-Link execution: **not authorized and not performed**
- Result: **host/source/ELF validation foundation verified**

## Health-confirm policy

The portable N15-F policy permits confirmation only when all conditions stay
true for the target-side 5000 ms window, polled every 250 ms. The host verifier
uses a 1000 ms fixture only to keep the deterministic matrix short:

- metadata is trusted `TRIAL_STARTED` for the expected uint64 generation;
- the secondary mapping is active;
- the AP supervisor reports healthy and fault-free;
- supervisor generation and fault count remain unchanged;
- monotonic time does not regress.

Any metadata drift, primary mapping, AP fault, generation/fault-count change,
ordinary backward clock jump or insufficient interval resets or restarts the
window. True uint64 wrap is handled explicitly.

The then-current format-1 `verify_bk7258_ota_health.py` passed 7 positive and
15 negative cases plus 5 continuity-reset scenarios, `-Werror`, GCC
`-fanalyzer`, and exact official source hashes.  That verifier and its unused
portable core were retired after the format-2 dual-bank implementation
replaced them. The successor format-2 verifier was also removed when the
whole custom N15/N17 lifecycle was retired; no metadata write gate was enabled
by the historical host test.

## Separately gated profile

`cp_nsh_ota + ap_smp_psram` is a validation-only profile. It is emitted under
`nuttx/bk7258-dual-ota-validation/` and cannot be selected accidentally by the
normal build defaults.

- all six Boot select/remap/trial compile and runtime constants are 1;
- CP staging and metadata write code is compiled in;
- both CP runtime write gates initialize false in BSS;
- every mutating `bkota` operation requires exact token
  `N15-WRITE-<generation>` and disarms both gates on exit;
- there is no automatic reset/reboot path;
- artifacts are explicitly unsigned and carry no authentication or
  anti-rollback claim;
- build reports retain `N15_OTA_BOARD_WRITE_AUTHORIZED=false`.

The validation verifier checks source contracts, official source hashes,
final Boot/CP symbols, gate values, BSS initialization and diagnostic strings.
Its latest result is PASS with Boot gates 1 and CP runtime gates false.

## Volatile candidate transport

The 2,576,384-byte candidate cannot fit the 1 MiB LittleFS partition. N15-F
therefore defines a validation-only, fixed upper-PSRAM transfer window:

| Content | Address | Size |
|---|---:|---:|
| candidate pair | `0x60800000` | `0x00275000` |
| descriptor | `0x60a75000` | 384 bytes |
| pending record | `0x60a76000` | 512 bytes |
| exclusive end | `0x60a76200` | — |

The target rejects the path unless PSRAM is ready, MPU-valid and at least
16 MiB. The normal profile still treats the upper 8 MiB as boot-tested and
unallocated; no allocator or persistence API was opened.

The descriptor is exported independently from metadata and checked byte for
byte against record bytes 124..507. The transfer verifier validates all file
sizes, generations, reports and SHA-256 values and emits a reviewable J-Link
command file. The WSL2 loader is dry-run by default, accepts only fixed
addresses, and requires both `--watchdog-stopped` and `--execute` for a real
volatile load. Its J-Link command block contains only halt, three
load/verify pairs, go and exit—no reset or Flash command.

The standard NuttX watchdog ioctl is used only after the exact generation
token. Exact v3.1.1.9 source verification proves `bk_wdt_stop()` clears the
driver init state and later feed calls fail closed instead of restarting it.
After `prepare-transfer`, a physical reset is mandatory to restore the normal
watchdog state; J-Link reset is forbidden before stage/publication because it
would discard the PSRAM payload.

Generation-42 validation artifacts:

| Artifact | Size | SHA-256 |
|---|---:|---|
| `s_app-candidate.bin` | 2,576,384 | `67d4fbade38d3a5fd78f6dfe2b0380f8040200883f54f9b167b46aacb9eb460b` |
| `bk7258-ota-stage.bin` | 384 | `c425cad6ba35e91bbd5e8dce100300a7d02a4d60eac2a8ac405de7e794722f99` |
| `bk7258-ota-pending-record.bin` | 512 | `f8c68f75da8cc2d324eda5d93121e04f72f4e6836a25cf7fa2d692b0c527cdba` |
| `bk7258-ota-transfer.json` | 1,472 | `b6a4c487baf8c520d2251db0944f0e73b3c8e6a873d99b2be282d52a20094196` |

The loader dry-run and transfer verifier passed. Actual J-Link loading,
Flash staging, publication, remap, health confirmation, rollback, wear and
deterministic fault/controlled-power-cycle cases remain N15-V board work under
fresh owner authority. Randomly timed or mid-Flash-pulse power cuts are outside
the prepared SOP. The deployed board remains at the N15-M A-only baseline.

The later N15-V host closure added deterministic target failpoints and a
15-identity ordered campaign without changing this transport ABI. See the
[N15-V host verification](2026-08-04-n15-v-host-fault-injection.md).

## Final host audit

After the target/host timing wording and current sparse-flash documentation
were corrected, the complete N15-A..F host matrices, normal Boot/CP ELF
closure, validation verifier, transfer verifier and loader dry-run were run
again and passed. Python compilation, shell syntax, `git diff --check` and the
project-memory checker also passed. Official NuttX and apps trees are
zero-diff; the active v3.1.1.9 CP/AP imported bundles pass their checksum
manifests, and the N15 verifiers pass the pinned external SDK source hashes.
