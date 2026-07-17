# RV1126B Board Verify Example — P2-A Mailbox Verification

Real-world board verification instance from the RV1126B openvela P2-A RPMsg/RPTun adaptation.

## Verification Context

- **Firmware**: Route A build (nuttx.bin SHA-256: `14c1c409...f779c1`)
- **Flash method**: Full `update.img` upgrade
- **Target**: RV1126B HPMCU (RV32IMC) ↔ Linux A-core

## Verification Results

### 1. Boot Verification

```
NuttX: NuttX 0.0.0 e02f581e23 Jul 16 2026 23:52:35 risc-v rv1126b_evb
Linux: Linux sportcam 6.1.x ... Alientek RV1126B AMP Board with HPMCU
```

### 2. Device Nodes

```
/dev/rptun/ap    — EXISTS (RPTUN registered)
/dev/rpmsg/ap    — EXISTS (RPMsg device created after Linux handshake)
```

NuttX `ps` showed:
```
25  0  224  FIFO  Kthread  Waiting  Semaphore  rpmsg-ap-0
```

### 3. Mailbox Registers

```
MBOX7 A2B (Linux→HPMCU):
  A2B_INTEN  = 0x00000101  (bit0=1 RX enabled, bit8=1 TRIG_MODE)
  A2B_STATUS = 0x00000000  (consumed)
  A2B_CMD    = 0x00000003  (link_id)
  A2B_DATA   = 0x524D5347  ("RMSG" magic)

MBOX4 B2A (HPMCU→Linux, vqid0):
  B2A_INTEN  = 0x00000101  (bit8=1 TRIG_MODE, bit0=1 TX_DONE)
  B2A_STATUS = 0x00000000  (consumed)

MBOX7 B2A (HPMCU→Linux, vqid1):
  B2A_INTEN  = 0x00000101
  B2A_STATUS = 0x00000000
```

### 4. IRQ Counters (Before/After rpmsgchar)

**Before** (Linux `/proc/interrupts`):
```
93:  0  0  0  0  GICv2 141 Level  20d00000.mailbox
94:  0  0  0  0  GICv2 144 Level  20d30000.mailbox
```

**After** (`rpmsgchar -c /dev/rpmsg/ap -n rpmsg-demo`):
```
93:  1  0  0  0  GICv2 141 Level  20d00000.mailbox   ← +1 (vqid0 doorbell)
94:  0  0  0  0  GICv2 144 Level  20d30000.mailbox   (no change)
```

### 5. Channel Enumeration

**Before rpmsgchar**:
```
/sys/bus/rpmsg/devices/virtio0.rpmsg_ctrl.0.0
/sys/bus/rpmsg/devices/virtio0.rpmsg_ns.53.53
```

**After rpmsgchar**:
```
/sys/bus/rpmsg/devices/virtio0.rpmsg_ctrl.0.0
/sys/bus/rpmsg/devices/virtio0.rpmsg-demo.-1.1024   ← NEW
/sys/bus/rpmsg/devices/virtio0.rpmsg_ns.53.53
```

Linux dmesg:
```
[31.713523] virtio_rpmsg_bus virtio0: creating channel rpmsg-demo addr 0x400
```

### 6. Data Path

```
NuttX: rpmsgchar -c /dev/rpmsg/ap -n rpmsg-demo
  → rpmsgchar [5:100]
  → Start the echo test
  → (blocks on read — Linux has no echo client, expected)
```

## Verdict

| # | Check | Result |
|---|-------|--------|
| 1 | Boot | PASS |
| 2 | Device nodes | PASS |
| 3 | Registers | PASS |
| 4 | IRQ counters | PASS (0→1 on MBOX4 B2A) |
| 5 | Channel enum | PASS (rpmsg-demo created) |
| 6 | Data path | PASS (NS announce sent, blocks on read — expected) |

## Key Observations

1. **Full update.img required for first flash**: Standalone amp.img failed once (specific flash anomaly), full update.img worked reliably.
2. **rpmsgchar blocks by design**: Without Linux echo client, NuttX rpmsgchar blocks on `read()` after sending NS announce. This is expected behavior.
3. **IRQ 93 = MBOX4 B2A**: HPMCU vqid0 → MBOX4 B2A doorbell → Linux GIC IRQ 93. This is the HPMCU→Linux TX path.
4. **IRQ 94 = MBOX7 B2A**: HPMCU vqid1 → MBOX7 B2A doorbell → Linux GIC IRQ 94. Not triggered by rpmsgchar (which uses vqid0 only).
