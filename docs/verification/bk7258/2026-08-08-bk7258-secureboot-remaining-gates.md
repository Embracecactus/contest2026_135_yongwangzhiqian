# BK7258 Secure Boot remaining-gates verification

Date: 2026-08-08  
Scope: N17-SB reversible BL1 -> NuttX MCUboot BL2 -> CP/AP chain  
SDK boundary: official v3.1.1.9 only; NuttX/apps and SDK sources unchanged

This record closes the implementation and regression evidence that can be
obtained without programming OTP/eFuse. It does **not** claim recovery of the
undocumented BK7258 BootROM Manifest ABI or production hardware anti-rollback.

## 1. BL1 root-key gate

The board-owned candidate Manifest verifier reads the already observed Dubhe
OTP shadow only. It never writes OTP, eFuse, lifecycle or debug-lock state.
When the development board reports `LCS=CM` and an all-zero secure-boot key
hash, BL1 uses the compiled, recoverable development root. A non-zero OTP hash
would have to match the Manifest key; otherwise the candidate is rejected.

The packer now applies the same rule before it emits a page:

```text
random EC256 key  -> packaging error (root is not board-owned)
development root  -> 0xd5-byte candidate record + 0x1000-byte erased page
```

Host evidence:

- a freshly generated random EC256 key was rejected by
  `make_bl1_manifest.py`;
- `/tmp/bk7258-bl1-manifest-dev-key.pem` produced the accepted development
  root page;
- no private key is stored in the repository.

Board evidence:

- The deliberately random-key page was flashed only to the two Manifest
  sectors. Both BL1 attempts stopped at `B1PAGE` with
  `bl1 manifest rc 0x00000004`, which is the board-owned root-anchor reject.
  Capture: `logs/bk7258-auto-debug/20260807-234454`.
- The page regenerated from the anchored development root was then flashed
  through the normal sparse path. BL1 accepted it and the chain reached
  `B1PAGE -> B2INIT -> B2GO -> B2GORET -> B2GOOK -> B2SELB -> B2APOK ->
  B2HANDOFF -> NuttShell`. Capture: `logs/bk7258-auto-debug/20260808-001121`.

This proves the board-owned root policy and the negative path. It does not
prove that BK7258 BootROM uses these offsets or this key format.

### 1.1 Official v3.1.1.9 Manifest sample (read-only tool evidence)

The official v3.1.1.9 checkout contains BK7258 `bl1_ec256_{pub,priv}key.pem`,
`bl1_sign.py`, and the `secure_boot_tool` binary even though the board CSV
keeps `secureboot_en=FALSE` and `bl1_control.json` keeps
`security_boot_ena=0`.  Running that tool on a temporary 0x200-byte BL2
sample produced a 0xd5-byte record.  No SDK file or key was changed and the
private key was not copied into this repository.

The observed bytes are consistent with the candidate parser already used by
the board-owned BL1:

| Offset | Size | Meaning observed from `secure_boot_tool` |
|---|---:|---|
| 0x00 | 4 | `0xa1bc2fd8` magic, little-endian |
| 0x04 | 4 | layout `0x00010001` |
| 0x08 | 4 | Manifest version |
| 0x0c | 4 | total record size `0xd5` |
| 0x10 | 4 | `0x00030619` = EC256/SHA-256 |
| 0x14 | 4 | image count `1` |
| 0x20 | 4 | static/XIP address |
| 0x24 | 4 | load address |
| 0x28 | 4 | raw image byte length |
| 0x2c | 4 | entry address |
| 0x30 | 32 | SHA-256 of the raw image bytes |
| 0x54 | 65 | SEC1 public key (`0x04 || X || Y`) |
| 0x95 | 64 | ECDSA-P256 `r || s` |

For this sample the tool emitted ROTPK hash
`23f2f12a1b98726897172fdf9f02b35fe61d17ad83a341171f622cef62cff5d5`,
which is SHA-256 of the 65-byte SEC1 public point.  It emitted Manifest
digest `ea5e519be15984f3f8a226b9bd5537689e6835047825c93a57f4e02774dac9ea`,
which is SHA-256 of bytes `0x00..0x94`; the signature at `0x95` verifies over
that digest.  This is strong SDK/tool evidence for the record layout and
hash convention, but it still is not BootROM acceptance evidence because the
BK7258 secure-boot enable bit and OTP root are not provisioned on the board.
The board packer now defaults its candidate Manifest version/security-counter
field to `5`, matching the official `bl1_sign.py` call. A temporary candidate
generated from the current BL2 matched the official record's common header,
descriptor, raw-length and image-digest fields; only the development root key
and signature intentionally differed.

The stronger same-key check is now complete.  With the current 10,708-byte
BL2 and the same temporary P-256 key, official and project outputs were both
`0xd5` bytes and bytes `0x00..0x94` were identical.  All differences were in
the randomized ECDSA `r||s` field at `0x95..0xd4`.  The staging-layout path
derived Primary BL2 XIP `0x02004c00`; the official tool again produced an
identical `0x00..0x94` prefix.  Thus the host Manifest encoder is closed;
only BootROM consumption and hardware root provisioning remain open.

## 2. MCUboot security-counter gate

`MCUBOOT_HW_ROLLBACK_PROT` is enabled in the board-owned BL2 configuration.
The current backend is deliberately a development-only read-only floor:

```text
BL2_SECURITY_COUNTER_FLOOR=0       -> accept a valid signed counter, persist nothing
BL2_SECURITY_COUNTER_FLOOR=N       -> reject counters below N, persist nothing
```

The floor is compile-time and is not an OTP/NV counter. A production backend
still requires the unpublished TrustEngine/OTP/NV ABI and a power-loss-safe
monotonic update implementation.

Host harness results:

- floor `0`: `get=0`, update requests `0` and `UINT32_MAX` both return success;
- floor `0x12010002`: `get=0x12010002`, counter `0x12010001` returns reject,
  counter `0x12010002` returns success;
- the BL2 source recompiles with `-Wall -Wextra` without the earlier constant
  comparison warning.

### 2.1 Recoverable board negative gate

The hardware gate first exposed and corrected a test-assumption error.  The
configured image counter `18` is not stored as the integer `18`: direct,
read-only J-Link inspection of the active B image found the protected TLV at
mapped address `0x020366ac`:

```text
08 69 0c 00 50 00 04 00 01 00 01 12
```

The final four bytes are the little-endian `IMAGE_TLV_SEC_CNT` value
`0x12010001`.  Consequently development floors `19` and `20` correctly
rejected A but still selected B; those runs were observations, not negative
gate passes.

The exact negative package was therefore rebuilt with the read-only floor
`0x12010002`, one greater than the observed image counter.  Only the boot
envelope and the two BL2 envelopes were written.  The serial trace stopped at:

```text
B1PRIMARY -> B2INIT -> B2GO -> B2GORET
-> B2TRYA -> B2ARET -> B2TRYB -> B2BRET -> B2BAD
```

There was no `B2HANDOFF`, NuttShell, HardFault or ASSERT.  Evidence:
`/home/lijian/project/open-vela/logs/bk7258-floor-gate-negative/20260808-121619`.
This closes the recoverable BL2 anti-rollback rejection path; it does not
claim a persistent or hardware-backed counter.

The exact pre-test floor-zero boot and BL2 envelopes were then restored.  An
independent RTS reset reached:

```text
B1PRIMARY -> B2INIT -> B2GO -> B2GORET -> B2GOOK
-> B2SELA -> B2APOK -> B2HANDOFF -> NuttShell
```

Evidence:
`/home/lijian/project/open-vela/logs/bk7258-floor-gate-recovery/20260808-121819`.
CP/AP application images, B slot, LittleFS, metadata, Manifest pages, policy,
calibration, OTP and eFuse were not written.

The full negative build also exercised an explicit
`BL1_MANIFEST_RAW_PAGE=false`.  The build entry point now validates the public
boolean spelling once and passes numeric `0`/`1` to the BL1 C build, avoiding
an invalid preprocessor expression while preserving the user-facing option.

The normal package uses `MCUBOOT_SECURITY_COUNTER=auto`, and `imgtool
dumpinfo` shows a protected `IMAGE_TLV_SEC_CNT` of `0x12010001` in both CP and
AP members. The board regression above reached NSH, proving the counter TLV
is accepted by the integrated BL2 floor-zero path.

The board-owned `flash_map_backend` was also rebuilt with defensive checks for
null handles, null destinations and invalid sector-count pointers.  A full
32-way CP/AP `mcuboot` build completed after that change; no NuttX or SDK source
was edited.

## 3. Official AES/CRC/merge/sign order

`pack_bk7258_secureboot.py` now emits a separate, explicit host-reference
artifact for the order reconstructed from the official v3.1.1.9 source
(`bl1_sign.py`, `bl2_sign.py`, and `partition.py`):

```text
logical CP/AP placement -> pinned NuttX MCUboot sign/pad
-> optional AES on signed primary_all -> 32+2 CRC
-> physical tail/status placement
```

The package contains `primary_all_code.bin` (logical merged input),
`primary_all_code_signed.bin` (pinned NuttX imgtool output),
`primary_all_code_prepared.bin` (the post-AES stream; identical to the signed
stream when AES is disabled),
`primary_all_code_signed_crc.bin` (un-padded CRC stream),
`primary_all_code_crc_padded.bin` (full physical pair), and
`secureboot-pipeline.json`. The report records the official `0x24f000` signed
slot, the AP code offset `CP logical size - 0x1000`, the optional AES start
address, and the two `0xefbeadde` XIP status words. It marks the result
`host-reference-only`, `hardware_verified=false`, and
`otp_efuse_written=false`. No AES key or SDK executable is copied into the
repository. The AES branch has now been host-executed with the SDK's fixed-key
test configuration; the SDK tool returns status 1 after producing a valid
same-size stream, so the adapter accepts only status 0/1 together with an
exact output-size check. This proves the host tool invocation and ordering,
not BK7258 BootROM acceptance or the production key/OTP contract.
The final host run reported AES start `0x10000`, prepared/signed size
`0x24f000`, CRC stream size `0x273f00`, and physical pair size `0x275000`; the
prepared hash differed from the pre-AES signed hash.

The packer no longer hard-codes the active layout's `0x11000` application
start. It consumes the project secureboot staging CSV and derives both slot
geometries. The no-AES current-firmware run produced Primary ALL at
`0x049000`, Secondary ALL at `0x2be000`, signed size `0x24f000`, CRC stream
size `0x273f00`, and physical pair size `0x275000`. Both slots used the
pinned NuttX `imgtool.py`; their reports remain `hardware_verified=false` and
neither artifact was flashed.

### 3.1 Complete 8 MiB staging factory image

`assemble_bk7258_secureboot_factory.py` now assembles the complete staging
layout without signing or device access. The successful current-firmware
image has SHA-256
`54b91ec7517eb66a94d5b637b9a73550f5e9211f4f4377bd50c4e92fddda4e32`
and the following placements:

```text
bl1_control         0x000000..0x003000
primary_manifest   0x003000..0x004000
secondary_manifest 0x004000..0x005000
primary_bl2         0x005000..0x027000 (code starts 0x0050c0)
secondary_bl2       0x027000..0x049000 (code starts 0x0270c0)
primary_all         0x049000..0x2be000
secondary_all       0x2be000..0x533000
```

Assembly validates the control magic/addresses and safe debug bits, both
Manifest BL2 addresses/sizes/digests, every 32+2 CRC packet in both ALL
images, both MCUboot magic words, and both XIP status pairs. Corrupting byte
zero of Primary ALL was rejected at encoded offset zero by the CRC16 check.
The artifact is explicitly labelled `secureboot-staging-host-only`,
`legacy_bootable=false`, `hardware_verified=false`, and was not flashed.

The official boot-control sector cannot be persistently rewritten by the
current legacy XIP BL1: at raw offset `0x1000` that sector is still part of
the running legacy boot envelope. Erasing it would modify BL1 itself. The
current implementation therefore keeps boot preference read-only and uses
verified Primary/Secondary fallback. Persistent boot-flag ownership remains
with the immutable BootROM secure layout (or a future separately proven,
non-overlapping control region), not the active legacy image.

The full build entry point can opt into the same branch without embedding a
key: set `SECUREBOOT_AES_TOOL` and `SECUREBOOT_AES_KEY_FILE` alongside
`MCUBOOT_OFFICIAL_PIPELINE=YES`. If either is absent, the build emits the
no-AES reference and never invents a key.

The board-flashed path remains the separately proven CP/AP MCUboot pair
(individual signed images, then the existing board 32+2 placement). The
host-reference merged stream is **not** flashed or described as BootROM
bootable until the exact BK7258 CRC/AES read view is known.

### 3.2 Executable legacy bridge board closure

The owner authorized overwriting the project-owned Flash ranges. A fresh
32-way build used the active legacy-compatible layout rather than pretending
the host-only three-page control image was directly executable. Its chain is:

```text
BK7258 legacy BootROM -> 68 KiB CRC-expanded board BL1
-> embedded signed Primary/Secondary Manifest
-> Primary/Secondary NuttX MCUboot BL2 in SRAM
-> same-slot signed CP/AP pair -> NuttShell
```

The build used MCUboot version `18.1.2`, security counter `19`, the pinned
NuttX `imgtool.py`, and external temporary development keys. No private key
entered the repository or logs. Artifact hashes were:

- factory prefix: `b81989c8de1d924ae32785881c853ceec5496d29ab410b89f213d8a0e2a231a4`;
- restored BL1: `828787261583c2bc21d05207404c602e9b21789a7307490050741aa068fb45f2`;
- BL2 CRC image: `535571b677f0ced7d2c8a49b2495fbc0b2778657dfab50cb732c56a106204f17`.

The bounded factory write covered `0x000000..0x4fc000`, both 16 KiB BL2
segments at `0x51d000` and `0x53f000`, and the authorized LittleFS clear at
`0x600000..0x700000`. `usr_config`, reserved ranges and the official
calibration tail were preserved. The downloader reported `Writing Flash OK`
and `{All Finished Successfully}`. Capture
`logs/bk7258-secureboot-bridge/20260808-163101` reached:

```text
B1PRIMARY -> BL2RAM -> B2INIT -> B2GOOK
-> B2SELA -> B2APOK -> B2HANDOFF -> NuttShell
```

For a hardware-negative gate, only byte `0x40` of the Primary Manifest copy
was changed and the boot envelope was regenerated with valid 32+2 CRC. The
boot-only write produced exactly:

```text
bl1 manifest rc 0x00000002 -> B1PRIMARY BAD -> B1SECONDARY
-> BL2RAM -> B2SELA -> B2APOK -> B2HANDOFF -> NuttShell
```

Evidence: `logs/bk7258-secureboot-bridge-negative/20260808-163314`.
The valid BL1 was immediately restored; capture
`logs/bk7258-secureboot-bridge-restored/20260808-163401` returned to Primary.
An independent 150 ms COM7 RTS physical reset then passed the same Primary
chain with `cold_path=yes` in
`logs/bk7258-secureboot-bridge-rts/20260808-163433`.

This closes the recoverable software-rooted BL1/BL2/MCUboot chain. It does
not claim the host-only 12 KiB control layout is directly BootROM-bootable,
nor does it claim OTP-rooted Secure Boot.

### 3.3 Minimal executable BL1 closure

The MCUboot package now builds Tier-1 with `BL1_MINIMAL=1`. Its production
responsibility is intentionally limited to clock/reset and watchdog handling,
Manifest authentication, deterministic Primary-to-Secondary BL2 fallback,
checked SRAM policy publication, BL2 copy/vector validation and handoff.
Historical N15/N17 lifecycle readers, the OTA Flash writer, N17 release keys
and the NuttX ECC wrapper are not linked into this profile. The existing
validation profiles remain available separately.

The final Manifest-enforcing ELF contains 9,878 bytes of text/rodata and zero
data/BSS. A profile-aware `nm` build gate requires the BL1 verifier/handoff
symbols and rejects `boot_ota_*`, `boot_n17_*`, N17-key and NuttX-ECC symbols.
The complete v3.1.1.9 CP/AP build passed with `JOBS=32`, MCUboot version
`18.1.3` and protected counter `20`; host mailbox/BL1-policy tests passed with
0/31 failures.

The final board evidence is:

- valid Primary: `logs/bk7258-secureboot-minimal-primary/20260808-164835`;
- Primary Manifest digest byte `0x40` corrupted with a valid 32+2 envelope:
  `rc=2 -> B1PRIMARY BAD -> B1SECONDARY -> B2HANDOFF -> NuttShell` in
  `logs/bk7258-secureboot-minimal-negative/20260808-165028`;
- valid boot restored: `logs/bk7258-secureboot-minimal-restored/20260808-165102`;
- independent COM7 RTS cold path: `cold_path=yes`, Primary and NuttShell in
  `logs/bk7258-secureboot-minimal-rts/20260808-165125`.

The board is left on the valid Primary image. No OTP/eFuse, lifecycle or
debug-lock state was written. This is a software-rooted executable baseline,
not a claim of official BK7258 BootROM Secure Boot equivalence.

## 4. A/B reject, rollback, and corruption matrix

The implementation and earlier captures cover the reversible matrix:

| Case | Expected result | Evidence |
|---|---|---|
| valid A pair | handoff to A | `2026-08-07-mcuboot-pair-gate-current-board.md` |
| valid B pair / A invalid | B remap and handoff | `2026-08-07-mcuboot-b-slot-remap-current-board.md` |
| primary BL2/Manifest digest corruption | BL1 tries secondary | `2026-08-07-bl2-primary-secondary-fallback-board.md` |
| Manifest signature corruption | BL1 rejects before BL2 | `2026-08-07-beken-manifest-candidate-board.md` |
| CP/AP cross-slot-only state | no handoff; fail closed | `2026-08-07-mcuboot-cp-ap-cross-slot-reject-board.md` |
| CP/AP version mismatch | `B2GENBAD`, then fallback | `2026-08-07-mcuboot-cp-ap-same-generation-reject-board.md` |
| CP/AP protected-counter mismatch | same pair gate rejects | `bk7258_bl2_main.c` pair-counter check; host floor test above |
| malformed signed AP vector | pair rejected before handoff | `2026-08-07-mcuboot-b-ap-vector-reject-board.md` |
| floor above both signed image counters | A and B rejected; `B2BAD`, no handoff | board gate in section 2.1 |

The physical OTP/NV rollback transition is intentionally absent. Therefore
the table is a recoverable development matrix, not a production anti-rollback
certification.

## 5. Still blocked / deferred

The following cannot be honestly marked complete from the available material:

1. BK7258 BootROM acceptance of the now tool-matched Manifest and its OTP
   root-key binding;
2. exact BK7258 AES key/nonce/register contract and CRC decode order as seen by
   BootROM/BL1;
3. persistent TrustEngine/OTP/NV security-counter provisioning and rollback;
4. secure-boot lifecycle activation and irreversible debug/OTP operations.

The read-only J-Link OTP shadow probe and the BK7236 documentation remain
semantic/source evidence only. SB-H stays closed until Beken supplies the
BK7258 artifact/ABI or a separately authorized, recoverable hardware test
procedure is agreed.
