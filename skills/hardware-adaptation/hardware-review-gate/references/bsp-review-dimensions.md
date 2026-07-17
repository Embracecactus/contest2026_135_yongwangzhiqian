# BSP Review Dimensions — Checklist

Detailed checklists for the four review dimensions in `hardware-review-gate`.

## Dimension 1: Concurrency Safety

### Shared State Access
- [ ] All ISR-accessed fields protected by spin_lock_irqsave or equivalent
- [ ] All task-shared fields protected by mutex/spinlock
- [ ] No check-then-act without lock (TOCTOU)
- [ ] Lock ordering consistent across all call paths

### IRQ Protection
- [ ] up_disable_irq before modifying IRQ-related state
- [ ] irq_detach in deinit paths
- [ ] No callback invocation between disable and clear

### Memory Barriers
- [ ] UP_DSB/UP_DMB after MMIO writes (especially W1C)
- [ ] UP_DSB before reading back MMIO state
- [ ] DCache flush/invalidate for shared memory regions

### Race Windows
- [ ] init failure rollback: reverse order of init
- [ ] callback NULL window: disable IRQ before clearing callback
- [ ] concurrent init: check-then-set under single lock

## Dimension 2: Hardware Registers

### MMIO Semantics
- [ ] All MMIO accesses use getreg32/putreg32 (volatile)
- [ ] No caching of MMIO values across state changes
- [ ] Device memory ordering assumptions documented

### Read-Modify-Write
- [ ] RMW sequences protected by lock when shared
- [ ] W1C bits: read STATUS, then W1C (not blind write)
- [ ] Hiword write-enable: upper bits as write mask

### Split-Access
- [ ] 64-bit registers: two 32-bit writes documented and ordered
- [ ] CMD-then-DATA: DATA write triggers hardware (CMD must be stable first)
- [ ] No 64-bit atomic assumptions unless hardware guarantees

### Register Definitions
- [ ] Base addresses match CMSIS/datasheet
- [ ] IRQ numbers match INTMUX/source mappings
- [ ] Register offsets verified against SDK headers

## Dimension 3: Startup & Linking

### Init Sequence
- [ ] board_late_initialize calls init function
- [ ] Init function validates prerequisites (clock, power, memory)
- [ ] Init returns 0 on success, negative errno on failure

### Failure Rollback
- [ ] Reverse order of init steps
- [ ] irq_detach on failure
- [ ] kmm_free on failure
- [ ] State reset to UNINIT

### Section Layout
- [ ] NOLOAD sections not in binary
- [ ] MEMORY regions adjacent/aligned
- [ ] ASSERTs catch boundary violations
- [ ] Heap endpoints exclude reserved regions

### Symbol Resolution
- [ ] All init functions resolve when CONFIG enabled
- [ ] No unresolved symbols when CONFIG disabled
- [ ] Make.defs/CMakeLists.txt match source files

## Dimension 4: Build Consistency

### Kconfig
- [ ] Symbol dependencies correct (depends on / select)
- [ ] Default values appropriate
- [ ] No circular dependencies

### Make.defs
- [ ] New source files conditionally included
- [ ] CONFIG guards match Kconfig

### CMakeLists.txt
- [ ] In sync with Make.defs (or intentionally disabled)
- [ ] SRCS list matches Make.defs

### defconfig
- [ ] savedefconfig produces consistent output
- [ ] No symbols stripped that should be retained
- [ ] CONFIG values match intended defaults

### Linker Constants
- [ ] MEMORY regions match Kconfig defaults
- [ ] C compile-time checks match linker ASSERTs
- [ ] No drift between defconfig, .config, and linker script
