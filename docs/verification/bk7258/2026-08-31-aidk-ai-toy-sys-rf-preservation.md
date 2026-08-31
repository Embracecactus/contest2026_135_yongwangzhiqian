# AIDK AI Toy `sys_rf` preservation acceptance

Date: 2026-08-31
Board: AIDK AI Toy
Scope: device-bound direct-boot recovery diagnostic

## Accepted result

The AIDK AI Toy `sys_rf` preservation path is accepted for the tested device.
The complete 4 KiB partition at raw Flash `0x7fe000` had this SHA-256 in all
four observations:

```text
b6cb961f09a3ad8c3c5d749a27c63dbb010015f67cb06c77ea37f211c8cee781
```

| Observation | SHA-256 result |
|---|---|
| Same-device accepted-base readback | match |
| Device-bound 8 MiB diagnostic full image | match |
| First post-flash boot snapshot | match |
| Second post-flash boot snapshot | match |

Both boot traces contained only overlapping `READ` observations. Neither trace
contained an overlapping `ERASE` or `WRITE`. The temporary wrappers observed
the SDK raw-Flash boundary and did not modify Flash data.

## Corroborating boot evidence

Both post-flash boots reported the same:

- RF calibration flag: `valid`;
- Flash TX-power table selector: `0xf`;
- crystal calibration value: `31`;
- Wi-Fi MAC: `c8:47:8c:cb:7f:80`;
- BLE MAC: `c8:47:8c:cb:7f:81`;
- TLV/table heads and bounded hashes.

The per-boot `calibration_main()` bias/timing measurements were not identical.
That is runtime adjustment, not persistent factory provisioning: the complete
partition digest remained identical across both runs and no write was observed.

The optional polar-table range at `0x7fec00` remained erased and produced the
same magic-code warning on both boots. This condition is part of the accepted
base and was preserved; this acceptance does not reinterpret it as a package
failure or claim that an absent optional table has been generated.

## Product boundary

This result proves byte-for-byte preservation and runtime read-only behavior
for this device. It does not prove RF power, frequency error, EVM, sensitivity,
regional compliance, or the provenance of the original factory calibration.

A dense 8 MiB recovery image necessarily contains bytes for `sys_rf`. It is
therefore bound to the same device and materialized from that device's accepted
base. A programmer may physically erase/program those addresses when writing a
dense image even though their final bytes are unchanged. An image that must not
address the immutable tail at all is a different, bounded recovery artifact;
it is not an 8 MiB dense image.

Normal builds and OTA do not own or generate `sys_rf`. A universal dense image
must not embed this device's RF/MAC state; it remains unavailable until a
reviewed per-unit manufacturing provisioner exists.

## Diagnostic retirement

After capture, the temporary `BRFT` source, Kconfig option, build-system hooks,
linker wrappers, and board-profile enablement were removed.

The subsequent clean direct-boot build passed for CP, AP, BL1, image assembly,
and the BK7258 layer gate. The resolved CP configuration and clean CP ELF
contain no diagnostic option, trace source name, or `BRFT` string.

The device-bound clean delivery is version `0.1.0+5`:

| Artifact or range | Size | SHA-256 |
|---|---:|---|
| `rf-closed-clean-current.zip` | 12,562,854 bytes | `2490715fba19412b598cee32545ecb89cc6471fe213ca63e99c62e55dfed67c2` |
| `aidk_ai_toy-v0.1.0-5-full-flash.bin` | 8,388,608 bytes | `ec7b98f703bd4f273e2868adab7c5d72b83ee636b6641a7f60b702b33ee117dd` |
| Full-image `sys_rf` slice | 4,096 bytes | `b6cb961f09a3ad8c3c5d749a27c63dbb010015f67cb06c77ea37f211c8cee781` |

`bk7258.py verify delivery` passed. The release manifest records `sys_rf` as
`preserved` and `device-unique`, with identical before/after hashes. The package
is explicitly unsigned, direct-boot, `requires-provisioning`, has no OTA, and
is a same-device recovery/debug artifact rather than a production release.

The maintained product contract is
[RF calibration and factory provisioning](../../platforms/bk7258/rf-calibration-and-factory-provisioning.md).
