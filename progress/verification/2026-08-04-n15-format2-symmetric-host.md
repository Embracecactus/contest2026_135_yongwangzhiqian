# N15 format-2 symmetric OTA host verification

- Date: 2026-08-04
- Scope: repository-owned OTA only; no board access or Flash write
- SDK: official `bk_avdk_smp-release-v3.1.1.9`
- Result: **PASS / host packaging and independent verification**

## Verified lifecycle

The ordered campaign now contains 16 distinct identities. The first 15 retain
candidate corruption, staging/publication/trial failures, rollback, health
refusal and successful A-to-B confirmation. The terminal case takes the
preceding confirmed-B candidate as the stable base, stages inactive A and
confirms A:

```text
generation 314: A -> PENDING_B -> TRIAL_B -> CONFIRMED_B
generation 315: B -> PENDING_A -> TRIAL_A -> CONFIRMED_A
```

The terminal metadata records:

- `target_slot=a`, `base_slot=b`, `state=pending_a`;
- `base_version=n15v314-confirm`;
- `base_pair_representation=crc-container`;
- `base_pair_sha256` equal to generation 314's complete
  `s_app-candidate.bin` SHA-256.

## Commands

```bash
python3 board/bk7258_t5ai/scripts/pack_bk7258_ota_campaign.py \
  --validation-package /home/lijian/project/open-vela/nuttx/bk7258-dual-ota-validation \
  --output /tmp/bk7258-ota-symmetric.Q7krkf/campaign \
  --sdk-source /home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9 \
  --start-generation 300 --base-version n15-base --start-timestamp 1000

python3 board/bk7258_t5ai/scripts/verify_bk7258_ota_campaign.py \
  --campaign /tmp/bk7258-ota-symmetric.Q7krkf/campaign \
  --sdk-source /home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9 \
  --expected-start-generation 300 \
  --output /tmp/bk7258-ota-symmetric.Q7krkf/campaign-verification.json
```

## Results

- campaign pack: 16/16 PASS;
- independent verifier: 16/16 PASS;
- loader dry-runs: 16/16 PASS;
- candidate, descriptor and metadata identities: 16 unique each;
- generation range: 300..315;
- campaign manifest SHA-256:
  `deb90033c96c6c62776acdd3874caedc2adb1fc788629f9cf76be25c8b3efa56`;
- `board_write_authorized=false`;
- `physical_execution_performed=false`.

Generations 300..315 are isolated host-test identities. No board-validation
procedure was executed. This evidence proves deterministic packaging,
cross-package base identity and verifier behavior; it does not prove physical
Flash timing, remap, reset or power-loss recovery.
