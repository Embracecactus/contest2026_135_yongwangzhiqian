> **Historical / retired design:** the custom N17 journal, policy sector and
> migration implementation were removed from active source on 2026-08-10.
> Preserve this file only as architecture history. The current BL1/MCUboot A/B
> update design is separate and is documented by the platform SOP; do not use
> this file as a partition, journal, signing, or migration source.

# BK7258 N17 Manifest placement, format-3 journal, and migration design

- Status: Accepted and frozen architecture; host layout/vector/format-3 model
  complete, firmware and board-write gates closed
- Date: 2026-08-06
- Scope: N17-S recoverable software-root architecture for the historical Tuya
  T5AI-Core layout only; it never established a multi-board contract
- Approval evidence: project owner explicitly accepted all three decisions
  together on 2026-08-06
- Authority: partition source/generator, public vector, portable format-3
  model and host verification; no Bootloader format-3 implementation,
  firmware build, Flash write, OTP/eFuse operation, or board action is
  authorized
- Historical prerequisite: `memory/decisions/ADR-008-n17-phased-ota-authentication.md`
  and the frozen
  [512-byte signed Manifest ABI](n17-signed-manifest-abi.md)

## 1. Findings first

### F1 — Manifest placement changes the signed layout identity

Before this decision, the CSV ended the second metadata bank at `0x50b000`
and left `0x50b000..0x600000` unallocated. The accepted CSV now assigns the
three N17 sectors below, which changes `layout_sha256`. The public Manifest
vector has been reissued against that accepted layout without changing the
byte-level ABI.

Decision: every new Flash owner is defined in the repository CSV and all
consumers are generated from it; the addresses are not hidden in
bootloader-only constants.

### F2 — Format 2 cannot be reinterpreted in place

Format 2 fills its 512-byte record with a mutable lifecycle header, stable
pair identity and an embedded 384-byte unsigned candidate descriptor. It has
no signed-Manifest digest or accepted security-counter floor. Treating it as
format 3 would create ambiguous trust semantics.

Recommendation: use a new magic and format, and allow the migration
bootloader to parse both formats only while the signed policy remains
unarmed.

### F3 — “No format-3 record” is not a durable armed-state indicator

If erased format-3 banks meant “legacy mode,” erasing or corrupting both banks
after migration could restore the unsigned format-2/A fallback. The system
needs a separate monotonic software arming indication.

Recommendation: reserve one read-only-to-normal-code 4 KiB policy sector.
All `0xff` means unarmed. Any programmed bit means armed, so a torn marker can
never reopen unsigned boot. N17-S still cannot resist an external raw-Flash
attacker erasing the entire sector; that remains an explicit software-root
limitation.

### F4 — Confirmation and the software floor must be one journal commit

Writing `CONFIRMED` and then advancing a separate counter, or doing those
operations in the reverse order, creates a reset window that can either allow
a downgrade or make both slots unbootable.

Recommendation: the terminal `STABLE/CONFIRMED` record changes the stable
slot and `accepted_security_counter` together in one append-only record. The
CRC-containing 32-byte program unit is written last.

## 2. Physical layout decision

### Options

| Option | Failure and recovery | Layout/tooling | Decision |
|---|---|---|---|
| Embed Manifest in each executable pair | Creates a digest self-reference or excludes bytes from publisher authorization; couples Manifest rewrite to executable data | Conflicts with the frozen full-pair digest | Reject |
| Store Manifests inside the two journal banks | Journal reclamation can erase the only authorization for a slot | Couples immutable and mutable lifetimes | Reject |
| Use separate erase sectors in the existing reserved span | Inactive Manifest can be replaced without touching the active image or journal; policy marker can be one-way | Costs 12 KiB and changes the generated layout hash | Recommend |

### Accepted CSV rows and addresses

The executable slots, both current metadata banks, vendor `usr_config`,
LittleFS and the calibration tail retain their current addresses.

| Role | Raw range | Size | Normal write policy |
|---|---:|---:|---|
| format-3 bank 0 | `0x4fb000..0x4fc000` | 4 KiB | lifecycle writer only |
| vendor `usr_config` | `0x4fc000..0x50a000` | 56 KiB | unchanged vendor owner |
| format-3 bank 1 | `0x50a000..0x50b000` | 4 KiB | lifecycle writer only |
| `ota_manifest_a` | `0x50b000..0x50c000` | 4 KiB | only while slot A is inactive |
| `ota_manifest_b` | `0x50c000..0x50d000` | 4 KiB | only while slot B is inactive |
| `ota_auth_policy` | `0x50d000..0x50e000` | 4 KiB | normal writes/erase forbidden; migration gate only |
| remaining reserved | `0x50e000..0x600000` | `0xf2000` | unallocated |
| LittleFS | `0x600000..0x700000` | 1 MiB | unchanged CP owner |

The accepted canonical rows are:

```csv
ota_manifest_a,,4K,data,TRUE,TRUE,ota_manifest_a
ota_manifest_b,,4K,data,TRUE,TRUE,ota_manifest_b
ota_auth_policy,,4K,data,TRUE,FALSE,ota_auth_policy
```

The regenerated canonical model calculates:

- layout ID: `bk7258-v3119-ab-32d3519eada0a7f7`;
- layout SHA-256:
  `32d3519eada0a7f77a284998e785fdb1daa55c691b3bfaf1a92b4097ce398203`.

The official v3.1.1.9 internal partition IDs `0..8` and existing project IDs
`s_app=9`, bank 0 `=10`, and bank 1 `=11` remain stable. The three new rows
become IDs `12..14`, so project-added LittleFS moves from ID `12` to `15`.
Current repository consumers use the generated role macro, and the immutable
SDK could not have compiled against the project-only LittleFS ID. Even so,
source and final-ELF checks for a literal old ID are an implementation gate.

### Manifest sector protocol

The first 512 bytes contain exactly the frozen Manifest ABI. The remaining
3584 bytes must be erased when the writer publishes it. Updating one slot is
ordered as follows:

1. Prove the target slot is inactive.
2. Stage and read-back/hash the complete inactive CP/AP raw pair.
3. Erase only that inactive slot's Manifest sector.
4. Program the 512-byte Manifest in aligned 32-byte chunks and read it back.
5. Parse its canonical fields, verify its signature, layout and all payload
   digests against the inactive slot.
6. Only then publish a `PENDING` record in the inactive journal bank.

The active slot, its Manifest and the selected lifecycle bank are never
erased during these steps.

## 3. Software arming marker

The policy sector is one erase sector, but its canonical marker occupies one
32-byte Flash program unit:

| Offset | Size | Field | Value |
|---:|---:|---|---|
| `0x00` | 8 | `magic` | ASCII `BKOTA17A` |
| `0x08` | 2 | `version` | little-endian `1` |
| `0x0a` | 2 | `size` | little-endian `32` |
| `0x0c` | 4 | `flags` | zero |
| `0x10` | 12 | `layout_tag` | first 12 bytes of the accepted layout SHA-256 |
| `0x1c` | 4 | `crc32` | little-endian CRC32 over bytes `0x00..0x1b` |

For the accepted layout, the canonical 32 bytes are:

```text
424b4f5441313741010020000000000032d3519eada0a7f77a2849981a6fa31b
```

Boot classifies the complete 4 KiB sector, not only the first 32 bytes:

- every byte `0xff`: `UNARMED`;
- exact marker plus erased remainder: `ARMED_CANONICAL`;
- any other non-erased byte: `ARMED_DEGRADED`.

Both armed states forbid format-2 and unsigned-A fallback. A torn marker is
therefore recoverable through already verified format-3 data but can never
decrease policy. Normal firmware and normal OTA must have no erase/write path
to this sector.

## 4. Format-3 journal ABI

Each 4 KiB bank contains sixteen 256-byte append-only records. All integer
fields are unsigned little-endian. Digests use normal 32-byte SHA-256 output
order. Empty record slots are all `0xff`; every reserved field in a written
record is zero.

| Offset | Size | Field | Rule |
|---:|---:|---|---|
| `0x000` | 8 | `magic` | ASCII `BKOTA17J` |
| `0x008` | 2 | `format_version` | `3` |
| `0x00a` | 2 | `record_size` | `256` |
| `0x00c` | 1 | `phase` | `1=STABLE`, `2=PENDING`, `3=TRIAL` |
| `0x00d` | 1 | `stable_slot` | `0=A`, `1=B` |
| `0x00e` | 1 | `target_slot` | opposite slot for pending/trial; `0xff` for stable |
| `0x00f` | 1 | `outcome` | see canonical combinations below |
| `0x010` | 4 | `flags` | zero; unknown flags reject |
| `0x014` | 2 | `digest_algorithm` | `1=SHA-256` |
| `0x016` | 2 | `reserved16` | zero |
| `0x018` | 8 | `sequence` | starts at 1 and increments by exactly 1 |
| `0x020` | 8 | `generation` | non-zero; fixed within one bank lifecycle |
| `0x028` | 8 | `accepted_security_counter` | current software downgrade floor |
| `0x030` | 4 | `created_timestamp` | diagnostic; fixed within the generation |
| `0x034` | 4 | `reserved32` | zero |
| `0x038` | 32 | `slot_a_manifest_sha256` | SHA-256 of Manifest A signed region |
| `0x058` | 32 | `slot_b_manifest_sha256` | SHA-256 of Manifest B signed region |
| `0x078` | 32 | `previous_record_sha256` | zero for sequence 1; otherwise hash of preceding complete record |
| `0x098` | 96 | `reserved` | zero |
| `0x0f8` | 4 | `commit_magic` | raw ASCII `CMT3` |
| `0x0fc` | 4 | `crc32` | CRC32 over bytes `0x000..0x0fb` |

Canonical `outcome` values are:

- `0=NONE`: required for `PENDING` and `TRIAL`;
- `1=FACTORY`: permitted only for an initial `STABLE` record;
- `2=MIGRATED`: permitted only for a migration `STABLE` record;
- `3=CONFIRMED`: terminal stable record after a successful trial;
- `4=ROLLED_BACK`: terminal stable record after a failed or consumed trial.

Both Manifest digests remain fixed throughout one generation. They identify
the first 448 signed bytes, not the randomized ECDSA signature bytes. A
record is committed by programming its first seven 32-byte units, programming
the final unit containing `commit_magic` and CRC last, then reading back and
parsing all 256 bytes. Until that final unit is complete, both the explicit
commit check and CRC reject the record. A record slot is written only once.

### Bank and transition rules

- A bank contains records from exactly one generation.
- A normal generation begins with `PENDING`. A valid target proceeds through
  `TRIAL` to exactly one terminal `STABLE/CONFIRMED` or
  `STABLE/ROLLED_BACK` record. A rejected or operator-cancelled target may go
  directly from `PENDING` to `STABLE/ROLLED_BACK` without consuming a trial.
- `stable_slot` remains the base during pending/trial. Confirmation changes it
  to the target; rollback leaves it at the base. A terminal record clears
  `target_slot` to `0xff`.
- The accepted floor remains equal to the authenticated stable Manifest's
  counter through pending/trial/rollback. Confirmation atomically changes it
  to the authenticated target Manifest's counter.
- A target counter must be strictly greater than the current floor before
  `PENDING` may be published.
- A new generation is written into the bank that does not hold the currently
  selected durable lifecycle. The old bank remains unchanged until the new
  first record passes full read-back validation.
- Structurally valid banks with different generations select the greatest
  generation. Equal generations in different banks are ambiguous and reject.
- A torn or invalid newer bank cannot displace the older valid bank.
- When both banks are eligible, a newer normal generation is accepted as a
  successor only if the older bank ends in `STABLE` and the new `PENDING`
  preserves that stable slot's Manifest digest and accepted floor. The second
  migration baseline must preserve both Manifest digests, stable slot and
  floor. A higher number without this lineage is invalid and cannot lower the
  floor or replace the older bank.

```text
                 append TRIAL before jumping to target
 STABLE(base) ──publish──> PENDING(base,target) ──> TRIAL(base,target)
      ^                   │                               │
      │                   └── reject/cancel ──────────────┤
      │                         health PASS               │ reset/failure
      ├──── STABLE(target, CONFIRMED, floor=target) <─────┤
      └──── STABLE(base, ROLLED_BACK, floor=base)  <──────┘
```

On a later boot, a persisted `TRIAL` boots the stable base rather than granting
a second target attempt. The stable application then appends the rollback
terminal before another update can begin.

### Boot decision rules

The bootloader verifies journal structure before using it, but no mutable
journal field authorizes executable bytes by itself. Manifest eligibility is
phase-specific so rewriting an inactive slot cannot invalidate the still
selected stable release:

1. Always load the stable slot's Manifest and require its signed-region digest
   to match that slot's journal field. The other Manifest is required only
   when granting a target trial or attempting a permitted fallback.
2. Verify Manifest canonical form, key, low-S ECDSA signature, product,
   board, chip and compiled layout.
3. Hash and validate the actual slot before it can boot.
4. Require the chosen Manifest counter to be at least the record floor.

For `PENDING`, the stable Manifest is mandatory, while an invalid target boots
that authenticated stable base and does not consume a trial. The stable
application must then append `STABLE/ROLLED_BACK` before another update may
begin. For persisted `TRIAL`, boot the stable base. For `STABLE`, only the
named stable slot is mandatory; its inactive Manifest may be erased or
replaced while preparing a new generation. If the stable slot fails, the
other slot may be used only when its journal-referenced signed Manifest and
actual payload are valid and its counter is not below the accepted floor.
Otherwise boot fails closed; an older signed release is not an anti-rollback
recovery path.

## 5. Format-2 to format-3 migration

The retired N17 Tier-1 bootloader design was single-copy, so installing the new dual-format
bootloader is a controlled wired/factory prerequisite, not a normal in-field
self-update. The new bootloader initially supports both formats and leaves the
policy sector erased.

The migration then runs in this order:

1. Parse the selected format-2 bank and fully validate its stable slot.
2. Copy or stage that signed release into the inactive slot so A and B contain
   the same verified CP/AP pair for the initial recovery baseline.
3. Write and verify the same slot-neutral signed Manifest in both dedicated
   Manifest sectors. Both counters equal the initial non-zero floor.
4. Erase only the non-selected format-2 bank and publish a
   `STABLE/MIGRATED` format-3 baseline at `old_generation + 1`.
5. Re-read both slots, both Manifests and the new journal through the future
   boot decision code.
6. Erase the remaining format-2 bank and publish the same stable identity as
   a second format-3 baseline at `old_generation + 2`.
7. Re-read both format-3 banks and prove either one can independently select
   an authenticated slot.
8. Verify the complete policy sector is erased, program its one 32-byte marker
   once, and read back the complete sector.
9. Reset and require an armed, format-3-only authenticated boot. Format 2 and
   header-only A recovery are now permanently disabled for normal operation.

Before arming, a complete eligible format-3 bank is preferred. If no eligible
format-3 bank exists, the dual-format bootloader may still use format 2. After
any bit in the policy sector is programmed, it may use only format 3.

### Reset/power-loss matrix

| Interruption point | Durable boot source |
|---|---|
| Before/during inactive slot copy | selected format-2 stable slot |
| During either Manifest write | selected format-2 stable slot |
| During first format-3 bank publication | untouched selected format-2 bank |
| After first format-3 bank is valid | authenticated format-3 baseline |
| During second format-3 bank publication | first valid format-3 bank |
| Before marker program | format 3 preferred; format 2 still allowed if no eligible format 3 remains |
| During marker program | any programmed bit requires the already verified format-3 baseline |
| After marker program | format 3 only; unsigned/header-only recovery forbidden |

This sequence never erases both durable lifecycle banks before one format-3
replacement is valid, and never arms signature enforcement before two signed
payload/Manifest paths are independently boot-eligible.

## 6. Security and recovery boundary

- Normal OTA can write only the inactive executable pair, its Manifest and the
  inactive lifecycle bank. It cannot write the active slot/Manifest, policy
  sector, bootloader, `usr_config`, LittleFS or calibration tail.
- A power loss cannot turn a partially programmed marker into `UNARMED`.
- Confirmation and floor advancement are one record commit. A torn commit
  leaves the previous trial/base decision authoritative.
- After confirmation, a valid but lower-counter old slot is deliberately not
  an automatic fallback. The development board remains recoverable with a
  wired full reflash because N17-S does not alter OTP/eFuse or disable J-Link.
- Erasing/replacing the complete Flash, bootloader, embedded public key or
  policy sector remains outside the N17-S threat model. N17-H is required for
  hardware-rooted downgrade resistance.

## 7. Acceptance and implementation gates

The owner accepted the Manifest-sector placement, 256-byte format-3 journal
and fail-closed migration together. Host-only integration has completed
without modifying official SDK, NuttX or apps:

1. The project CSV/generator roles and every generated layout consumer now
   carry the three accepted sectors.
2. The public Manifest conformance vector was reissued with the accepted
   `32d351...8203` layout hash; its 512-byte ABI is unchanged and the verifier
   still rejects all 3,600 negative cases.
3. The historical portable model at
   `board/bk7258/scripts/bk7258_n17_journal.py` implemented the exact record ABI,
   valid-prefix bank scan, transition,
   lineage, counter, phase-specific boot and one-way policy rules. Its host
   verifier covered every record bit, every byte-level torn-record/append cut
   and the migration reset matrix. The custom N17 runtime and scripts have since
   been retired, so this path is intentionally retained as historical text and
   is not a current repository link.

The proposed firmware, Flash-write and board gates were never enabled before
this design was retired. They are not pending work for the current BL1/MCUboot
A/B architecture.

## 8. Reversal signals

- Exact v3.1.1.9 evidence shows a vendor owner uses
  `0x50b000..0x50e000` or an immutable component depends on project LittleFS
  remaining numeric ID 12.
- The bootloader cannot scan the policy sector and authenticate two Manifests
  within its watchdog, Flash, SRAM or stack envelope.
- A 256-byte append cannot be made atomic enough with the verified 32-byte
  program primitive and old-bank fallback.
- Product requirements demand recovery to a lower signed counter after
  confirmation; that would require changing the accepted anti-rollback claim,
  not silently weakening this selector.
