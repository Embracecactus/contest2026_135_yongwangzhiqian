# N17 accepted layout and signed-manifest public vector verification

- Date: 2026-08-06
- Result: PASS (host-only layout and ABI gate)
- Hardware/firmware mutation: none
- SDK/NuttX/apps source changes: none

## Scope

This gate verifies the owner-accepted N17 partition placement and 512-byte
Manifest encoding before any Tier-1 C implementation. The checked fixture is
public-only and uses deterministic synthetic payload streams; it is not a
bootable RBL image.

## Command and result

```sh
python3 board/bk7258_t5ai/scripts/gen_bk7258_partitions.py \
  --check \
  --sdk-source /home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9
python3 board/bk7258_t5ai/scripts/verify_bk7258_partitions.py \
  --sdk-source /home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9
python3 board/bk7258_t5ai/scripts/verify_bk7258_sdk_partition_wrapper.py
python3 board/bk7258_t5ai/scripts/verify_bk7258_ota_layout.py
python3 board/bk7258_t5ai/scripts/verify_bk7258_ota_rotation.py \
  --sdk-source /home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9
python3 board/bk7258_t5ai/scripts/verify_bk7258_n17_manifest_vector.py
```

The official v3.1.1.9 partition parser/generator accepted the project CSV.
Partition, SDK-wrapper, OTA-layout and symmetric rotation host checks passed:

- layout ID: `bk7258-v3119-ab-32d3519eada0a7f7`;
- layout SHA-256:
  `32d3519eada0a7f77a284998e785fdb1daa55c691b3bfaf1a92b4097ce398203`;
- generated table entries: 16 including the bootloader entry;
- Manifest A/B and policy IDs: 12, 13 and 14; LittleFS role ID: 15;
- existing executable, metadata, vendor, LittleFS and calibration physical
  ranges unchanged.

OpenSSL independently accepted the reissued ECDSA P-256/SHA-256 positive vector. The
verifier rejected all 3,584 possible single-bit mutations in the 448-byte
signed region and 16 additional payload, identity, key/scalar, encoding,
canonicalization, reserved-byte, counter-floor and domain-separation checks.

- signed-region SHA-256:
  `5dd969feafce252c80dd244083836b46aad61f7d432789f4926442ad70ddfa1e`
- complete-manifest SHA-256:
  `613ff894d473a053e83b93f2091c37dda2952b9539ceb219d67507f0b5d1493e`
- negative checks: `3600`

## Boundaries

The ephemeral test private key existed only in one generator process and was
never serialized. Only its public point and precomputed signature remain in
the vector. This result verifies generated Flash placement only; it does not
verify a C parser, Bootloader format-3 integration, RBL decoding, physical
migration, board behavior, OTP/eFuse state or hardware-backed anti-rollback.
