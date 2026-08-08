# BK7236 BootROM deep reverse baseline for the BK7258 BL1/BL2 port

Date: 2026-08-08  
Scope: immutable BL1 BootROM ABI and BL2 handoff only  
Hardware activity: none  
Persistent security state: unchanged

## 1. Why this binary is useful, and what it does not prove

The official BK7236 security documentation says that BL1 is a closed BootROM
based on the Arm China security stack and that BL2 is MCUboot.  The official
`bk_idk` checkout contains the following matching reference image:

```text
path:   /home/lijian/project/armino/bk_idk/projects/security/
        secureboot_tfm_test/config/bk7236/bootrom.bin
size:   73544 bytes (0x11f48)
sha256: a52978aceaf105f0025185bdc42023ca622e248787a61551f51d4ccc4a2f2221
string: bootrom ver1.1
```

This is direct static evidence for the BK7236/IPSS BL1 implementation.  It is
not yet a dump of the BK7258 mask ROM.  In particular, its derived link base
must not be described as a BK7258 physical ROM address.

Evidence labels used below:

- **E3**: behavior read directly from the official BK7236 binary;
- **E2**: official host tool, wrapper or configuration evidence;
- **inference**: a name or hardware effect inferred from calls and data flow;
- **unproven on BK7258**: requires a BK7258 read-only mapping or reversible
  board experiment.

## 2. Address recovery

The raw binary was imported as little-endian Armv8-M Thumb code.  A small
Ghidra pre-script seeded functions from the vector table instead of relying
on speculative entry-point analysis.  At the derived SRAM-linked reference
base `0x28040000`, the table contains:

| Vector | Raw value | Recovered handler |
|---:|---:|---:|
| initial MSP | `0x28020000` | stack value, not code |
| Reset | `0x2804b5b9` | `0x2804b5b8` |
| NMI | `0x2804b57d` | `0x2804b57c` |
| HardFault | `0x2804b591` | `0x2804b590` |
| MemManage | `0x2804b587` | `0x2804b586` |
| BusFault | `0x2804b59b` | `0x2804b59a` |
| UsageFault | `0x2804b5a5` | `0x2804b5a4` |
| SecureFault | `0x2804b5af` | `0x2804b5ae` |
| IRQ 15 | `0x28042351` | `0x28042350` |

All eight non-stack vectors are odd Thumb addresses and land inside the same
`0x11f48`-byte image.  This satisfies the address-consistency gate.  It proves
that this reference binary was linked to execute from SRAM around
`0x28040000`; it does **not** prove where BK7258 exposes its immutable ROM.

## 3. Recovered BL1 call chain

The following names are semantic names assigned by this analysis.  Addresses
are in the derived linked view; file offsets are `address - 0x28040000`.

| Address | Offset | Proposed name | Direct evidence |
|---:|---:|---|---|
| `0x2804b5b8` | `0xb5b8` | `Reset_Handler` | vector table and runtime initialization |
| `0x2804b6c8` | `0xb6c8` | `bs_main` | official function-name literal plus staged platform initialization |
| `0x2804c38c` | `0xc38c` | `BootRom_main/main` | high-confidence module mapping: called after constructors, performs verification and final gate |
| `0x2804c4fc` | `0xc4fc` | `secure_boot_sample` | high-confidence module mapping plus exact init/verify/get-info/cleanup sequence |
| `0x28042a68` | `0x2a68` | `sec_boot_init` | exact public prototype and context initialization behavior |
| `0x28042ac8` | `0x2ac8` | `sec_boot_cleanup` | exact public prototype and context/OTP cleanup behavior |
| `0x28042af4` | `0x2af4` | `sec_boot_verify_imgs` | public API plus Primary/Recovery strings and both verification branches |
| `0x28042f34` | `0x2f34` | `sec_boot_get_img_info` | public API plus descriptor-to-public-structure field copy |
| `0x280431d4` | `0x31d4` | `sec_boot_verify_mnfst` | direct function-name reference; Manifest/root/signature/image/version checks |
| `0x280430b4` | `0x30b4` | `sec_boot_read_manifest_by_type` | direct function-name reference; obtains a type 1/2 partition and reads it |
| `0x28042fe4` | `0x2fe4` | `verify_image_digest` | hashes image and compares the expected digest |
| `0x2804314c` | `0x314c` | `read_otp_manifest_version` | reads OTP offset `0x88` and counts programmed bits |
| `0x2804c35c` | `0xc35c` | `validate_selected_vector` | compares selected MSP/entry with words at `0x02000000` |
| `0x28042654` | `0x2654` | `control_set_to_flash_not_enable_jtag` | exact two-register-bit operation in the official BK7236 calibration source |
| `0x2804266c` | `0x266c` | `control_set_to_flash` | enables debug, then calls `control_set_to_flash_not_enable_jtag` |
| `0x280426a4` | `0x26a4` | `BK3000_start_wdt` | exact main/AON watchdog write sequence in the official BK7236 download source |

The recovered high-level control flow is:

```text
Reset_Handler
  -> bs_main
     -> C constructors
     -> BootRom_main/main
        -> secure_boot_sample
           -> sec_boot_init
           -> sec_boot_verify_imgs
              -> sec_boot_verify_mnfst(primary or recovery)
                 -> read Manifest header and body
                 -> enforce OTP root and lifecycle policy
                 -> verify Manifest signature
                 -> verify every image digest
                 -> advance version when policy allows
              -> if first choice fails, verify the other choice
           -> sec_boot_get_img_info(context, 0, &image_info)
           -> sec_boot_cleanup
        -> validate selected MSP/entry against the active XIP vector
        -> perform the final hardware handoff sequence
```

The matching official header survives at
`components/tfm/tfm/platform/ext/target/beken/download/inc/secure_boot/`
`secure_boot_core.h`.  It defines the context as an opaque pointer and the
public image result as five fields in this order: static address, loading
address, entry address, size and encrypted-image boolean.  The decompiled
functions above implement those exact prototypes.  `sec_boot_get_img_num()`
is declared by the header but has no live call in this one-image BootROM path;
it was not assigned to an address without a direct binary match.

The final handoff is now closed for this BK7236 reference.  Descriptor 0's
entry field points to a vector table.  `0x2804c35c` compares that table's MSP
and Reset words exactly with the two words visible at `0x02000000`.  On
success BL1 calls `BK3000_start_wdt(0xa000)`, clears bit 9 and then bit 0 at
`0x44010008`, and spins until reset.  The watchdog helper writes `3` to the
main-WDT control register at `0x44800008`, performs the `0x5a`/`0xa5` pair at
its configuration register `0x44800010`, then repeats the paired write at the
AON-WDT register `0x44000600`.  The matching official BK7236 startup source
describes the two system bits as `flash_sel` and `boot_mode` and uses the
identical sequence for a hardware reset/remap handoff.  There is no direct
`BX`, `BXNS`, VTOR or MSP write in this success tail.

This is E3 evidence for BK7236 only.  The current official BK7258 v3.1.1.9
headers independently confirm all three register blocks and the same bit
meanings, so the register-level ABI is shared.  That source-level agreement
does **not** establish the missing BK7258 Secure-Boot policy, reset routing or
BootROM consumer.  Therefore the project must not copy this reset tail into
BK7258 merely from address compatibility.  Its reversible Flash BL1 retains
the proven direct Cortex-M handoff and only reuses the portable invariant that
the authorized and loaded vector words must match.

The portable invariant is implemented in `boot_bl1_handoff_core.[ch]`.  BL1
captures the authorized XIP MSP/Reset pair before copying BL2, compares it
with the loaded SRAM pair, and then applies the existing BK7258 stack and
execution-window checks.  Host ASan/UBSan coverage includes mismatched words,
alignment and both window boundaries.  A matching sparse board deployment
then reached:

```text
B1PRIMARY -> BL2RAM -> B2INIT -> B2GOOK -> B2SELA -> B2APOK
-> B2HANDOFF -> NuttShell
```

Capture: `logs/bk7258-auto-debug/20260808-025151/`; UART SHA-256
`93d0e3e412517e76a451312cd5081eaf8f817a5d3cd29c5d6fdff4af42cc7146`.
No `BAD`, `B1FAIL` or `bl2 copy vector` marker appeared.  This proves the
portable board gate, not the BK7236 reset/remap MMIO on BK7258.

## 4. Primary/Recovery policy

`sec_boot_verify_imgs` reads a preferred-boot value through
`0x2804d848`.  Type `1` and type `2` are the only accepted Manifest partition
types.

The earlier initialization function at `0x2804d768` reads exactly `0x28`
bytes from raw Flash offset `0x1000` into the BL1 boot_flag structure.  The
recovered first words are:

| Control offset | Meaning |
|---:|---|
| `0x00` | magic `0x4c725463` (`63 54 72 4c`) |
| `0x04` | preferred boot: `1` Primary, `2` Recovery |
| `0x08` | Primary Manifest XIP address |
| `0x0c` | Recovery Manifest XIP address |
| `0x10..0x24` | PLL/Secure-Boot/print/JTAG/FIH control booleans |

Official `partition.py` closes a naming ambiguity in the project notes.  The
XIP/overwrite profiles name the first raw 4 KiB region `bl1_control`; the
packer fills it with `0xff` and overwrites its first 64 bytes with the BL2
vector table.  The next raw 4 KiB region is `boot_flag`; its first `0x28`
bytes are the policy structure above.  The exact `secureboot_tfm_test`
artifact coalesces the first three 4 KiB regions into one 12 KiB
`bl1_control` partition, but preserves the same consumer offsets: vector at
relative `0`, policy at relative `0x1000`, and Primary Manifest immediately
after the 12 KiB area.  The vector and policy are separate consumers even
when the CSV combines their physical allocation.  The official packer does
not populate a valid `boot_flag` record in the normal XIP path, so an erased
record and BL1's compiled defaults are an expected configuration, not
evidence of a missing package step.

An invalid magic resets the structure to defaults.  An invalid preferred value
is normalized to Primary.  Manifest addresses must be in the accepted Flash
XIP window and separated by at least 4 KiB; otherwise the reference defaults
to Primary Manifest `0x02003000` and Recovery Manifest `0x02004000`.

Direct E3 behavior:

```text
preferred == 2:
  verify type 2
  if it fails, verify type 1

otherwise:
  verify type 1
  if it fails, verify type 2

both fail:
  return failure; do not hand off
```

This is supported both by control flow and the embedded messages:

- `Start Recovery Boot...`
- `Recovery Boot verified fail! Try Primary Boot...`
- `Start Primary Boot...`
- `Primary Boot verified fail! Try Recovery Boot...`
- `Both Primary and Recovery Boot verified fail !!!`

After success, `0x28042a34` records the selected type in bit 0 of
`0x4400000c`.  Its literal diagnostic is `bk: aon boot flag=%x`; the function
only performs a read/modify/write of that AON PMU register.  The analyzed
fallback path does not call a Flash erase/program routine and therefore does
not persist a new value into the raw `0x1000` `boot_flag` partition.

The accompanying BK7236 calibration source contains a separate
`hal_write_preferred_boot_flag()` implementation.  It updates the cached
record and calls `flash_op_write()`, whose partial-sector implementation reads
the whole 4 KiB sector, erases it, and writes the reconstructed sector.  No
second copy, generation, commit marker or recovery selection is present.
That source is useful ABI evidence, but it is neither present in the analyzed
success path nor a power-loss-safe persistence protocol.  BK7258 must not
invent a persistent boot preference from it.

The exact reference artifacts are from official `bk_idk` revision
`650e754e12fe`: BootROM SHA-256
`a52978aceaf105f0025185bdc42023ca622e248787a61551f51d4ccc4a2f2221`,
`hal_boot_control_7236.c` SHA-256
`5b767c9cd27e996c17a89ab8e65cddafce9dd151ab3cac9059f4585ade23566b`,
and `flash_operation.c` SHA-256
`0686d717faf76970185d06b3470c1313f7aa6a96dc5bbec808580171c9b77cd7`.

BK7258 headers expose the same AON PMU R3 address but describe bit 0 as
reserved.  A halt/read/resume J-Link snapshot on the current slot-A NuttX
chain returned `0x4400000c = 0x00000000`; `0x44010008` was also zero.  This is
consistent with the current software BL1 not using the BK7236 AON handoff
flag, but it does not establish a BK7258 bit-0 ABI.  No write probe was made.

## 5. Manifest consumer ABI recovered from BL1

The BootROM consumer independently confirms the generic IPSS producer
reversed from `secure_boot_tool`:

- first read is exactly `0x18` bytes;
- magic is `0xa1bc2fd8`;
- the complete record is subsequently read from a 4 KiB Manifest partition;
- the Manifest version is rejected when below the OTP version;
- encoded algorithm fields select hash, public-key hash, signature and image
  cipher handling;
- image descriptors are `0x18` bytes, followed by a digest whose length is
  selected by the image-hash algorithm;
- public key and signature lengths are selected from the algorithm fields.

Recovered key/signature sizes:

| Scheme family | Public key bytes | Signature bytes | Verifier |
|---|---:|---:|---|
| RSA-1024 | `0x80` | `0x80` | `0x2804d1dc` |
| RSA-2048 | `0x100` | `0x100` | `0x2804d1dc` |
| ECDSA-P256 | `0x41` | `0x40` | `0x2804ccf4` |
| ECDSA-P521 | `0x85` | `0x84` | `0x2804ccf4` |

The EC verifier requires SEC1 uncompressed public keys (`0x04 || X || Y`)
and verifies the signature using the selected SHA digest.  The RSA verifier
supports the encoded RSA/PKCS variants.  Invalid algorithms, sizes, public
keys, signatures or image digests fail closed.

This consumer-side result raises the `0xd5` EC256 single-image record from
“host-tool-only shape” to a high-confidence BK7236 BootROM ABI.  It still does
not prove that an unadapted BK7258 BootROM consumes the same record.

## 6. OTP and lifecycle behavior

The following offsets are parameters passed to the binary's OTP HAL; they are
BK7236/IPSS ABI evidence, not BK7258 register addresses:

| OTP HAL offset | Length | Meaning recovered from data flow |
|---:|---:|---|
| `0x28` | `0x20` | SHA-256 root-of-trust public-key hash |
| `0x68` | `4` | lifecycle encoding |
| `0x88` | `4` | one-way Manifest security-version bitmap |

These three values are now independently source-verified by the official
download-interface `hal_otp.h`: secure-boot public-key hash offset `40`
(`0x28`), lifecycle offset `104` (`0x68`), and the BK boot primary/recovery
shared Manifest-version offset `136` (`0x88`) with size four.  This confirms
the BK7236/IPSS HAL ABI; it still does not turn those offsets into BK7258 OTP
controller addresses.

Root behavior:

1. BL1 hashes the public key carried by the Manifest.
2. It compares that digest against the 32-byte OTP root hash.
3. A mismatch rejects the Manifest.
4. A blank OTP root is handled according to lifecycle/debug-bypass policy;
   it is not equivalent to production root binding.

That lifecycle policy is now closed for the reference binary.  Dubhe encodes
CM, DM, DD and DR as `0`, `1`, `3` and `7`; BL1 normalizes them to stages
`0..3` and rejects every other encoding.  The decision matrix is:

| Secure-Boot policy | CM/DM | DD/DR |
|---|---|---|
| disabled | Manifest-signature check may be skipped; image digests are still checked | fail closed |
| enabled, ROTPK hash blank | Manifest-signature check may be skipped; image digests are still checked | fail closed |
| enabled, ROTPK hash present | carried key hash must match ROTPK, then Manifest signature and image digests are checked | same full verification |

Whenever the Manifest-signature check is skipped, BL1 also skips advancing the
Manifest version in OTP.  Thus a development bypass cannot consume rollback
counter bits.  The current BK7258 Flash BL1 remains stricter: on an empty CM
root it verifies against its compiled development key instead of accepting an
arbitrary carried key.

Version behavior:

1. BL1 counts programmed one-way bits in the four-byte value at `0x88`.
2. A Manifest version below that count is rejected before image handoff.
3. After all image checks pass, a higher version may program additional bits
   through the OTP write HAL.
4. Values beyond the 32-bit bitmap are rejected.

The current BK7258 project must not call that write path until the user
explicitly authorizes an irreversible operation.  The read side is now closed
more narrowly: the official v3.1.1.9 BK7258 `otp1.csv` places the 32-bit BL1
counter at physical OTP `0x188`; the already verified Dubhe shadow mapping
subtracts physical base `0x100`, giving shadow offset `0x88` and absolute
address `0x4b111088`.  A halt/read/resume J-Link snapshot returned zero at
that address.  No OTP controller command, OTP write or eFuse access occurred.

The board BL1 therefore now reads only `0x4b111088` and applies the exact
BK7236 consumer rule: count consecutive one bits starting at bit 0 and stop at
the first zero.  A malformed bitmap such as `0b0101` consequently yields
floor 1, matching the recovered binary.  Host ASan/UBSan tests cover zero,
full, ordinary and non-contiguous bitmaps; the Arm BL1 still builds with
`-Werror`.  Because the current board value is zero, this adds a real
read-only rollback source without changing its accepted development image.

The enforced BL1 was then rebuilt with the already signed primary/secondary
BL2 records and written through the bounded boot-only sparse path.  Only raw
Flash `0x000000..0x011000` was replaced.  Capture
`logs/bk7258-auto-debug/20260808-080820` reached:

```text
B1PRIMARY -> BL2RAM -> B2INIT -> B2GOOK -> B2SELA -> B2APOK
-> B2HANDOFF -> NuttShell
```

The flashed boot SHA-256 was
`1ba63e4efbb5b6234a7150e929bd6e816835e007c3b5199bab3d46c77bab4399`.
This is target evidence that the early BL1 shadow read is accessible and
non-disruptive; it still does not authorize counter programming.

## 7. Image verification and optional transforms

For each image descriptor BL1 either verifies the static image directly or
copies/decrypts it according to the Manifest flags.  In every accepted path it
recomputes the selected hash and compares it with the descriptor digest.

The encrypted-image consumer order is now recovered more precisely from
`sec_boot_verify_mnfst` and the official crypto prototypes:

```text
read descriptor
  -> encrypted image with static_addr == loading_addr: reject
  -> copy static/XIP bytes to loading_addr
  -> select AES-CTR (802) or AES-ECB (800)
  -> use key type AES_BLOB (103), decrypt=true
  -> decrypt in place at loading_addr
  -> hash the resulting plaintext bytes
  -> compare that digest with the signed descriptor digest
```

The AES call also carries the official 16-byte IV and a returned destination
size which must still equal the image size.  Therefore the descriptor digest
binds the plaintext image, while the bytes stored at the static Flash address
may be ciphertext.  For a BL2 that is itself an MCUboot image, the portable
host order is consequently: construct/sign the MCUboot image, calculate the
outer BL1 descriptor over that plaintext signed image, then encrypt the stored
copy and apply the BK Flash transport encoding.  The outer Manifest signature
must still cover its descriptor and plaintext digest.  This is a BK7236/IPSS
consumer rule; the BK7258 key-blob provisioning and AES hardware ABI remain
unproven.

Version advancement is ordered after all descriptor hashes and the optional
extended-program hook.  A failed copy, decrypt, digest, extension, or OTP read
cannot reach the `hal_otp_write(0x88, ..., 4)` call.  The write assembles the
unary version bitmap and remains deliberately unused by the BK7258 project.

The binary also contains:

- optional extended-program validation and execution;
- AES/key-blob handling selected by Manifest flags;
- RSA and ECDSA Manifest verification;
- lifecycle-dependent bypass rules;
- duplicated FIH checks, canaries and fail-closed error flags.

This confirms the security-document architecture at the semantic level:
immutable BL1 authenticates policy and image metadata, then hands control to
an authenticated BL2.  It does not yet determine the BK7258 TrustEngine
register contract or the exact CP/AP pair model.

## 8. Tooling and bounded verification

`tools/ghidra/SeedArmVectorTable.java` was added as a generic raw Armv8-M
vector-table import helper.  It:

- accepts only 2..256 vector entries;
- seeds only odd Thumb handlers that fall inside the imported image;
- skips null, erased, even and out-of-image entries;
- fails when no valid handler can be established;
- treats its vector address as an analysis address, never as a physical-ROM
  claim.

The positive import recovered exactly eight valid in-image handlers from the
first 64 entries and allowed Ghidra to recover 352 functions.  Existing host
Manifest producer evidence was reused instead of creating a duplicate parser
or test suite.  A separate negative import passed an entry count of `1`; the
script rejected it at the argument gate with `invalid vector table arguments`
before any vector was seeded.  Ghidra itself reports script failure in its log
while returning success for the containing import, so acceptance is based on
the explicit script diagnostic rather than the process exit code.

## 9. What is now proven and what remains

Proven for the official BK7236 reference (E3):

- vector table and SRAM-linked address model;
- closed BL1 reset-to-verifier control-flow skeleton;
- IPSS Manifest header, algorithms and image-record consumption;
- OTP root-hash/lifecycle/version semantics;
- Manifest signature and per-image digest verification;
- preferred Primary/Recovery selection and failover;
- fail-closed behavior before the final handoff.

Still unproven for BK7258:

- the physical BootROM alias/window and whether it matches this binary;
- BK7258 OTP/TrustEngine numeric ABI;
- the hardware handoff/remap register meaning;
- BK7258-specific dual-core CP/AP image-pair rules;
- true BootROM acceptance with an OTP-bound root.

The semantic crosswalk to the BK7258 wrapper and board-owned BL1/BL2 contract
was completed after this baseline.  The remaining static work is limited to
recovering additional portable consumer rules and source names; the physical
BK7258 ROM mapping remains behind the separately recorded negative visibility
gate.

## 10. BK7258 final-reset register cross-check

The BK7236 BL1 success tail does not branch directly to the authenticated
image.  After comparing the first two vector words with the descriptor, it
arms the watchdog, clears bits 9 and 0 at `0x44010008`, then waits for a reset.
The official BK7258 v3.1.1.9 headers and the current official `bk_idk` agree
that this address is `cpu_storage_connect_op_select`:

- bit 0 is `boot_mode` (`0`: ROM boot, `1`: Flash boot);
- bit 9 is `flash_sel` (`0`: normal Flash, `1`: SPI download);
- `0x4401000c` is the separate `cpu_current_run_status` register.

The helper identities and the complete write sequence are now source-closed:

| Binary address | Recovered source identity | Exact operation |
|---:|---|---|
| `0x28042654` | `control_set_to_flash_not_enable_jtag` | clear `flash_sel`, then clear `boot_mode` at `0x44010008` |
| `0x2804266c` | `control_set_to_flash` | write `0x0f` to Arm `DAUTHCTRL`, then call the helper above |
| `0x280426a4` | `BK3000_start_wdt` | configure main WDT at `0x44800008/10` and AON WDT at `0x44000600` |
| `0x280426d6` | legacy reset-to-Flash wrapper | disable legacy UART/SPI state, arm both watchdogs, enable debug, clear the two mode bits, then spin |

`BootRom_main/main` does **not** call the debug-enabling wrapper.  Its
authenticated success edge is exactly:

```text
validate_selected_vector(selected_descriptor)
  -> BK3000_start_wdt(0xa000)
  -> control_set_to_flash_not_enable_jtag()
  -> spin until watchdog reset
```

That separation matters: the legacy download exit deliberately re-enables
debug, whereas the authenticated Secure-Boot edge leaves `DAUTHCTRL` under
the lifecycle/debug policy established earlier in reset.

BK7258 v3.1.1.9 confirms the underlying register ABI from independent source:

- `SOC_SYS_REG_BASE = 0x44010000`, with this field block at `+0x08`;
- `SOC_WDT_REG_BASE = 0x44800000`; control is at `+0x08` and configuration at
  `+0x10`;
- `SOC_AON_WDT_REG_BASE = 0x44000600`;
- its watchdog HAL uses the same paired `0x5a` then `0xa5` configuration
  writes.

This proves shared peripheral programming, not a completed BK7258 Secure-Boot
consumer.  After reset, the exact choice of ROM path, Flash remap and vector
activation is still a hardware/BootROM contract that the v3.1.1.9 BK7258 SDK
does not implement or document.

The BK7236 producer side nevertheless closes one more part of that contract.
The official `beken_utils` packer creates `bl1_control.bin` and copies exactly
the first 64 bytes of `bl2.bin` into its beginning; with random Flash AES it
copies the corresponding 64 bytes from `provision.bin` instead.  The
`secureboot_tfm_test` generated layout places this 12 KiB aggregate control
area at physical `0x20000`, the Primary Manifest at `0x23000`, and BL2 at
`0x24000`.  This explains why
the BootROM consumer checks the authenticated descriptor's first MSP/Reset
pair against the pair currently exposed at XIP address `0x02000000`: the
control partition deliberately supplies the reset-visible vector prefix.

Thus the static producer/consumer rule is now complete:

```text
BL2/provision vector prefix (64 bytes)
  -> bl1_control.bin at the reset-visible control partition
  -> active XIP vector at 0x02000000
  -> exact MSP/Reset comparison against the verified Manifest descriptor
  -> watchdog reset/remap only after the comparison succeeds
```

What remains unknown is narrower: the BK7258 hardware/ROM mapping that would
make an equivalent control partition reset-visible.  It is not the Manifest
format, watchdog register layout or vector-prefix producer rule.

This closes the earlier address-name ambiguity, but it does not prove that the
BK7236 reset tail is a usable BK7258 BL2 handoff.  A read-only J-Link snapshot
while the current BK7258 NuttX image was running returned:

- `0x44010008 = 0x00000000`;
- `0x4401000c = 0x00007060`;
- secure VTOR `0x28010800`.

Both reset-tail bits are therefore already clear in the current working boot
chain.  Repeating the clear operation would be a no-op rather than an observed
transfer mechanism.

A single reversible vector-catch experiment then preserved the existing
DEMCR value, enabled core-reset catch and armed the watchdog with period `1`.
It did not stop in BootROM or at a core reset.  After 500 ms the core was in
NMI at `0x020105a8` (`IPSR = 2`) and secure VTOR was still `0x28010800`.
This is evidence that the tested watchdog sequence first entered the current
NMI path; it is not evidence for a BK7258 BootROM alias or final handoff ABI.
No Flash, OTP or eFuse was written.

The probe was stopped at this negative gate.  An RTS reset recovered the board
and the capture at `logs/bk7258-auto-debug/20260808-025743` reached
`B1PRIMARY`, `B2INIT`, `B2HANDOFF` and NuttShell with verdict `PASS_NSH`.
Consequently, the project retains its authenticated direct SRAM handoff and
must not transplant the BK7236 watchdog/remap tail until a BK7258-specific
reset contract is demonstrated.

## 11. What the BootROM `debug` build actually enables

The Keil project labels the reference as `BUILD_TYPE="debug"`, defines
`CONFIG_DEBUG`, and compiles debug-level logging.  Static binary evidence does
not show a callable BootROM debug monitor or a secure-debug agent:

- Reset calls `0x2804c91c`, which writes `5` to Armv8-M `DAUTHCTRL`
  (`0xe000ee04`) and therefore starts with invasive debug disabled;
- `0x2804c910` writes `0x0f` to the same register, but its reachable callers
  are the legacy UART-download exit/switch-to-Flash paths;
- the authenticated Secure-Boot success tail calls the Flash-remap helper
  that does **not** enable `DAUTHCTRL`;
- `hal_secure_debug.c` is listed in the Keil project, but its FPGA flag
  address `0x40028008` and `Invalid secure debug cmd` string do not occur in
  the exact `bootrom.bin`, so that sample implementation was discarded or is
  unreachable in this artifact;
- the normal debug feature that is definitely present is controlled logging:
  ordinary flow logs and compact critical errors follow the eFuse/control
  policy documented for bits 1 and 7.

Thus BootROM `debug` is useful reverse-engineering evidence because symbols,
messages and verbose branches survived, but it is not an entry point that can
be invoked through J-Link.  The separate authenticated secure-debug protocol
belongs to MCUboot/BL2 in the official architecture and remains outside this
Flash BL1 slice.

## 12. BK7258 physical ROM visibility gate

The v3.1.1.9 BK7258 register header identifies secure ROM data base
`0x06000000` and the non-secure alias as base plus `0x10000000`.  The official
BL2 download code independently blocks software reads from
`0x06000000..0x06020000` and labels it BootROM.  Two bounded J-Link reads of
the first 64 bytes returned all `0xffffffff` at both `0x06000000` and
`0x16000000`; the running CPU was resumed after each read.

This is a negative visibility result, not proof that the mask ROM is empty.
The window may be hidden or powered down after startup.  The BK7236-only
calibration link address `0x061f0000` was not probed because no BK7258 source
maps BootROM there.  Runtime ROM dumping is therefore closed at this gate;
the project continues from the official BK7236 binary semantics plus
BK7258-specific Flash/TrustEngine evidence.

## 13. Official Keil project and source-name recovery

The official `bk_idk` revision cited above also contains
`components/secure_calibration/bk7236_bootrom.uvprojx`.  This is useful static
evidence, but it is not the project that produced the exact reference binary:

- the project defines `VERSION_STRING="Bootrom_v1.0"`, while the analyzed
  binary contains `bootrom ver1.1`;
- the project enables Arm compiler debug and browse information, but no AXF,
  MAP, object file or listing is present in the repository;
- its source list includes `hal_secure_debug.c`, while the exact binary has
  neither that sample's `Invalid secure debug cmd` text nor its FPGA flag
  address;
- the separate `calibration/` source tree is incomplete and does not contain
  the listed `secure_boot_core.c` or `secureboot_verifier.c`.

Consequently the Keil files are treated as an official module inventory, not
as a byte-identical source release.  The inventory nevertheless confirms that
the build architecture consisted of `bs.c`, `BootRom_main.c`,
`secure_boot_sample.c`, `secure_boot_core.c`, `secureboot_verifier.c`,
`hal_boot_control.c`, `hal_secure_boot.c`, the Dubhe OTP/crypto HALs and
`jump.c`.

The binary itself retains a number of C `__func__` strings used by diagnostic
paths.  Ghidra data references from those strings, combined with the adjacent
official calibration source, recover the following names without relying only
on semantic guesses:

| Address | Recovered official name | Static basis |
|---:|---|---|
| `0x28042654` | `control_set_to_flash_not_enable_jtag` | exact `SET_FLASHCTRL_RW_FLASH; REBOOT` expansion in the BK7236 calibration source |
| `0x2804266c` | `control_set_to_flash` | exact debug-enable wrapper around the preceding helper |
| `0x280426a4` | `BK3000_start_wdt` | exact main/AON watchdog addresses and paired key writes |
| `0x2804b6c8` | `bs_main` | its error-log file/function literal points at the function; initialization order also matches `bs.c` |
| `0x280430b4` | `sec_boot_read_manifest_by_type` | direct `__func__` reference at `0x280430e0` |
| `0x280431d4` | `sec_boot_verify_mnfst` | direct `__func__` reference inside the verifier; cipher-info and decrypt helpers are inlined into the same body |
| `0x2804c928` | `hal_crypto_aes` | `hal_crypto_aes` and `_set_sw_aes_key` references occur inside this function |
| `0x2804ccf4` | `hal_crypto_ecdsa_verify_digest` | direct function-name references throughout the P-256/P-521 verifier |
| `0x2804d0a0` | `hal_crypto_hash` | direct function-name references in the one-shot hash wrapper |
| `0x2804d1dc` | `hal_crypto_rsa_verify_digest` | direct function-name references in the RSA verifier |
| `0x2804d520` | `_flash_read_write` | direct function-name reference in the common raw-Flash helper |
| `0x2804d5f8` | `flash_op_read` | direct function-name references and the official full/partial-sector read shape |
| `0x2804d72c` | `check_manifest_addr` | instruction-for-instruction semantic match to the official static helper: Flash window plus 4 KiB separation |
| `0x2804d768` | `hal_ctrl_partition_load_and_init` | direct name reference plus the exact `0x1000`/`0x28` read and normalization logic |
| `0x2804d848` | `hal_read_preferred_boot_flag` | exact one-field getter matching the official source |
| `0x2804d858` | `hal_read_manifest_address` | exact two-address getter matching the official source/header ABI |
| `0x2804d8e0` | `hal_flash_read` | direct function-name references and XIP/raw-address conversion |
| `0x2804dce0` | `__dubhe_read_otp` | direct function-name references in the Dubhe read wrapper |
| `0x2804def8` | `__dubhe_write_otp` | direct function-name references in the Dubhe write wrapper |
| `0x2804e0b4` | `dubhe_otp_read` | direct function-name references in the validated OTP reader |
| `0x2804e328` | `dubhe_otp_write` | direct function-name references in the one-way OTP writer |
| `0x2804e8c0` | `hal_otp_init` | direct HAL name reference; the static `dubhe_otp_init` helper is inlined into this entry |
| `0x2804e958` | `hal_otp_cleanup` | exact reference-count decrement matching the official public cleanup function |
| `0x2804e96c` | `hal_otp_read` | direct name reference in the public read entry |
| `0x2804ea28` | `hal_otp_write` | direct name reference in the public write entry |
| `0x2804eaf8` | `hal_get_manifest_partition_info` | direct name reference plus type `1`/`2` and fixed 4 KiB size |

This narrows the earlier `Reset_Handler -> bootrom_main` wording.  The reset
runtime enters the official `bs_main`, which performs the staged device and
platform initialization, loads the boot-control record, initializes the
crypto/OTP side, runs the C constructor table, and then calls `0x2804c38c`.
The Keil module inventory names `BootRom_main.c`, while that function has the
normal `main()` position and owns the final infinite-loop/error boundary; it
is therefore a high-confidence `BootRom_main/main` mapping rather than a
surviving symbol.  Its child `0x2804c4fc` performs exactly the public
`sec_boot_init -> sec_boot_verify_imgs -> sec_boot_get_img_info(0) ->`
`sec_boot_cleanup` example sequence and maps with the same confidence to the
listed `secure_boot_sample.c`.  `main` then reads lifecycle offset `0x68`,
verifies that the authorized vector words match the active XIP vector, and
only then arms the reset/remap handoff.

One easy-to-miss ABI detail is now explicit.  The raw `boot_ctrl_data_t`
record stores Primary and Recovery Manifest addresses in adjacent words at
offsets `0x08` and `0x0c`.  The public `hal_manifest_addr_t` returned by
`hal_read_manifest_address()`, however, interleaves a size word after each
address: Primary at `+0`, Primary size at `+4`, Recovery at `+8`, Recovery
size at `+12`.  The binary function writes the two addresses at `+0` and `+8`,
and its caller zero-initializes the size holes.  These two structures must not
be cast onto each other.

The write-side OTP functions are part of the recovered official image, but
their presence is static evidence only.  They were not invoked by this work,
and no BK7258 OTP/eFuse operation is authorized by this name recovery.

## 14. Reset-entry and Flash-remap ownership

The exact `Reset_Handler` was compared block-for-block with the official
BK7236 `calibration/arch/arm-m/vector.c`.  Its startup order is:

```text
apply reset-time debug policy
  -> enable I-cache when configured
  -> read eFuse for deep-sleep/PLL policy
  -> on the deep-sleep fast-boot edge only, clear flash_sel/boot_mode
  -> set MSPLIM and VTOR for the SRAM-resident BL1
  -> copy .data and clear .bss
  -> optionally switch to the high-frequency clock
  -> bs_main
```

The ordinary authenticated path does not execute a second, previously hidden
Flash-remap setup.  `bs_main` closes/feeds the watchdog, initializes the system
HAL, UART, heap, Flash device, boot-control policy, crypto/OTP/TRNG and FIH
state, then enters `BootRom_main/main`.  Its only direct write to the
storage-connect register before authentication is legacy-download cleanup of
`flash_sel`; the final authenticated edge is the first ordinary path that
also clears `boot_mode`.

This rules out a software explanation for the remaining mapping gap.  The
static chain now proves both endpoints:

- producer: the packer installs the authenticated vector prefix in the
  reset-visible control area;
- consumer: BL1 compares its MSP/Reset words, arms both watchdogs, changes the
  two storage-connect bits and waits.

The transformation between those endpoints is performed by the BK7236
boot-mux/Flash-remap hardware or by immutable reset logic.  No additional C
function in this BootROM image programs an address-offset register to turn
physical `0x20000` into XIP `0x02000000`.  Recovering the corresponding
BK7258 behavior therefore requires a BK7258 reset/remap contract or a bounded
hardware experiment; further renaming of ordinary startup functions cannot
prove it.

The official BK7258 non-secure A/B bootloaders provide a useful negative
cross-check, but not that missing Secure-Boot contract.  Both the v3.1.1.9
baseline and the `bk_idk release/v2.0.1` sample arm the same watchdog ABI, then
program the BK7258 Flash-controller execute window and perform a direct
VTOR/MSP/`BX` handoff.  They do not expose the BK7236 `0x44010008`
watchdog-reset/remap success edge.  The detailed addresses and hashes are
recorded in `2026-08-07-ghidra-bk7258-ab-bootloader.md`.
