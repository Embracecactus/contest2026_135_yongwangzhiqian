---
name: firmware-gate
description: >
  Build/package/flash authorization gate for hardware adaptation firmware.
  Ensures pre-overwrite hash collection, backup creation, SDK link verification,
  and post-flash board verification. Use when: building firmware, 打包, 刷机门禁,
  firmware gate, SDK packaging, 构建门禁.
---

# Firmware Gate

Three-gate authorization process for firmware build → package → flash.

## When to Use

- User says "打包" / "构建门禁" / "刷机门禁" / "firmware gate"
- Before overwriting any firmware artifact (nuttx.bin, amp.img, update.img)
- Before flashing to target hardware

## Prerequisites

- Build artifacts exist (`$WORKSPACE/nuttx/nuttx.bin`)
- SDK path known (`$SDK` set)
- Target board accessible (for Gate 3)

## Three Gates

### Gate 1: Build Verification

**Before**: Clean build from source.

```bash
cd $WORKSPACE
./build.sh <board-config> distclean
./build.sh <board-config> -j$(nproc)
```

**Collect**:
```bash
bash <skill_dir>/scripts/hash-collect.sh "$WORKSPACE/nuttx"
```

**Verify**:
- [ ] Exit code 0
- [ ] No new errors (warnings acceptable)
- [ ] nuttx.bin size reasonable
- [ ] .linux_rpmsg section NOLOAD, not in binary
- [ ] Key symbols resolved (rptun_init, mailbox_init, rpmsg_create_ept)
- [ ] .config contains expected symbols

**Output**: Build report with hashes, sizes, warnings.

### Gate 2: Package Verification

**Before**: Overwriting SDK artifacts.

**Collect pre-overwrite hashes**:
```bash
bash <skill_dir>/scripts/hash-collect.sh "$WORKSPACE/nuttx" "$SDK/output"
bash <skill_dir>/scripts/sdk-link-check.sh "$SDK"
```

**Backup**:
```bash
cp -av "$RTT_TARGET" "$RTT_TARGET.before-<tag>"
cp -av "$FW/amp.img" "$FW/amp.img.before-<tag>"
cp -av "$UPDATE_IMG" "$UPDATE_IMG.before-<tag>"
```

**Verify SDK link**:
- [ ] `$OUT/rtt.bin` symlink resolves to expected target
- [ ] `$OUT/amp.its` exists and references correct files
- [ ] mkimage tool exists and is executable
- [ ] build.sh updateimg command exists

**Package**:
```bash
cp -av "$WORKSPACE/nuttx/nuttx.bin" "$RTT_TARGET"
cd "$OUT" && "$SDK/rtos/bsp/rockchip/tools/mkimage" -f amp.its -E -p 0xe00 "$FW/amp.img"
cd "$SDK" && ./build.sh updateimg
```

**Verify post-package**:
- [ ] amp.img hash changed from backup
- [ ] update.img hash changed from backup (if generated)
- [ ] rtt.bin target hash matches nuttx.bin

**Output**: Hash comparison table (before/after), backup paths.

### Gate 3: Flash Verification

**After**: Board verification (delegates to `board-verify` Skill).

```text
完整升级: $SDK/output/update/Image/update.img
或单独升级: $SDK/output/firmware/amp.img
```

**Post-flash checklist**:
- [ ] `uname` build time updated
- [ ] `/dev/rptun/<cpuname>` exists
- [ ] `/dev/rpmsg/<cpuname>` exists (if RPTUN registered)
- [ ] Mailbox registers as expected (board-verify)
- [ ] Linux IRQ counters updated (board-verify)

**Output**: Flash verification report.

## Rules

- Never overwrite without collecting pre-overwrite hash
- Never overwrite without backup (`.before-<tag>` suffix)
- Always verify new hash differs from backup (unless intentionally rebuilding same source)
- Gate 3 requires explicit user authorization to flash
- Build success ≠ board success — always verify on hardware

## References

- `references/rv1126b-sdk-packaging.md` — SDK packaging flow for RV1126B
