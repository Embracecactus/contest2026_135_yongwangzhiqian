# RV1126B SDK Packaging Reference

Real-world SDK packaging flow for the RV1126B openvela contest, extracted from the P2-A adaptation.

## Correct Packaging Chain

```text
$WORKSPACE/nuttx/nuttx.bin
  → copy to: readlink -f $OUT/rtt.bin (rttmcu.bin)
  → mkimage: $SDK/rtos/bsp/rockchip/tools/mkimage -f amp.its -E -p 0xe00 $FW/amp.img
  → optional: cd $SDK && ./build.sh updateimg → $OUT/update/Image/update.img
```

## Key Facts

- `$OUT/rtt.bin` is a symlink → `$SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/rttmcu.bin`
- The real copy target is the symlink resolution, not the symlink itself
- `$OUT/amp.its` defines the FIT image structure (kernel + DTB + load/entry addresses)
- `mkimage` is in the SDK toolchain, not `$SDK/hal/tools/mkimage`
- `build.sh updateimg` generates a full update.img including Linux + amp

## Flash Methods

1. **Full update.img** (recommended for first flash): `$OUT/update/Image/update.img`
   - Includes Linux kernel + AMP firmware
   - Guarantees consistent state

2. **Standalone amp.img** (for iterative AMP development): `$FW/amp.img`
   - Only replaces AMP firmware
   - Must verify with `uname` build time + `/dev/rptun/<cpuname>`
   - Previously failed once due to specific flash anomaly (not a general amp.img issue)

## Verification After Flash

```bash
# NuttX side
uname -a                    # check build time updated
ls /dev/rptun/<cpuname>     # RPTUN registered
ls /dev/rpmsg/<cpuname>     # RPMsg registered (after handshake)

# Linux side
dmesg | grep -Ei 'rpmsg|mailbox|virtio'   # kernel log
grep -Ei 'mailbox' /proc/interrupts        # IRQ counters
ls /sys/bus/rpmsg/devices/                 # channel enumeration
```

## Hash Chain Integrity

```text
nuttx.bin SHA-256
  = rtt.bin target SHA-256
  ≠ amp.img SHA-256        (mkimage adds FIT header)
  ≠ update.img SHA-256     (includes Linux + other components)
```

## Common Mistakes (Avoid)

- Using `$SDK/hal/tools/mkimage` (wrong tool)
- Using `$SDK/rtos/bsp/rockchip/rv1126b-mcu/Image/rtt.bin` directly (old path)
- Using `$OUT/amp.img` (99K historical artifact, not current)
- Forgetting to verify build time after standalone amp.img flash
