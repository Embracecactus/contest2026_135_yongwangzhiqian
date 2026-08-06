# N16 Wi-Fi STA and retained-service coexistence verification

- Date: 2026-08-06
- Branch: `feat/bk7258-n16-wifi`
- Base checkpoint: `0af5efe313ee2c56a88ec2c1d733de87a30af28e`
- SDK: checksum-pinned official Beken v3.1.1.9 CP/AP archives
- Result: N16 STA data plane, coexistence and bounded lifecycle matrix passed

## Scope and privacy

The dedicated `cp_nsh_wifi + ap_smp_wifi` profiles were rebuilt and sparse
flashed to the T5-AI board. Runtime credentials were entered with terminal
echo disabled. No credential is present in source, defconfig, this record or
the retained repository logs.

The sparse update wrote only boot, primary CP and primary AP. It preserved
LittleFS, the paired B image and the calibration tail.

## Final clean artifacts

| Segment | Bytes | SHA-256 |
|---|---:|---|
| `bl_crc.bin` | 69,632 | `54714e2f06365804e93ce17aa385d61d75225de536ad8f6d5a1cf63e88039ca6` |
| `app_crc_flash.bin` | 1,089,536 | `4cb834c1838ed72187085c6904d78b592fd526d55ad9936688b54f6488736f02` |
| `app1_crc_flash.bin` | 249,856 | `5ef7f836bd177f43392afb6de5801f6db87f67fa7e7c95d5cdd724a3094dae93` |

The full build passed SDK checksum verification, dynamic partition checks,
factory-layout boundaries and ELF-backed RPTUN layout verification. Tracked
official NuttX and apps source trees were clean after the build.

## STA and native network results

- Runtime-only association completed with `status=0`, connected link state,
  RSSI and a DHCP lease synchronized into AP `wlan0`.
- A deliberately wrong password failed boundedly with `-ETIMEDOUT`; a correct
  runtime credential attempt immediately afterwards succeeded. The control
  path now performs stop-before-restart and retires the stale lease/carrier
  before starting the next attempt.
- Gateway ICMP passed.
- Local operator-controlled TCP and UDP echo each passed four 64-byte payloads
  through normal NuttX sockets.
- The AP vendor lwIP/socket stack and AP DHCP client remain excluded. CP's
  official vnet controller owns DHCP; AP applies its reported lease to native
  NuttX networking.

## RPMsgFS failure and boundary fix

The first coexistence run failed RPMsgFS open/write with `-EBADF`. Temporary
observation-only instrumentation proved that a nested RPMsgFS allocation was
given the address of a still-live outer VFS `struct file`, overwriting its
inode and flags. No matching `free()` occurred, so this was not a close/free
race.

The permanent fix is `CONFIG_FS_HEAPSIZE=16384` in `ap_smp_wifi`. This uses
NuttX's supported independent FS heap for VFS/RPMsgFS/socket metadata and
isolates it from the vendor Wi-Fi/general AP heap. All temporary NuttX and
board diagnostic instrumentation was removed before the final build.

## Active-Wi-Fi coexistence result

With STA still associated and gateway ICMP passing:

- `bkrpmsgfstest all 1 30000`: all 1/64/464/1024-byte cases passed; AP and CP
  heap measurements were unchanged before and after every case.
- `bkrpmsgtest run 20 64 idle 10000`: both AP logical CPUs sent and received
  all 20 messages with zero errors; AP heap measurements were unchanged.
- `bkbttest info`: controller information and feature exchange passed.
- `apctl status`: AP `READY`, RPTUN `CONNECTED`, supervisor `HEALTHY`, CPU2
  `SCHEDULER_ONLINE`, and SMP/affinity/semaphore gates `PASSED`; supervisor
  fault and recovery counters remained zero.

## Lifecycle and retained-baseline closure

- With Wi-Fi active, `apctl restart 3000` returned `-EBUSY` (`-16`) before an
  AP reset. AP generation stayed at 1, AP remained `READY`, RPTUN remained
  `CONNECTED`, CPU2 remained online, supervisor faults stayed zero, and Wi-Fi
  status plus gateway ICMP continued to pass.
- Three consecutive COM7 RTS resets each produced `PASS_NSH`. After every
  reset, AP was `READY`, RPTUN `CONNECTED`, CPU2 `SCHEDULER_ONLINE`, SMP gates
  `PASSED`, and supervisor fault/recovery counts zero.
- Credentials correctly did not survive reset. After the third reset, a final
  hidden-input association obtained a lease and gateway ICMP passed.
- A final `bkrpmsgfstest run 1 64 10000` passed its write/read/checksum gate
  against CP LittleFS.
- Existing host verifiers passed the dynamic 12-partition layout, factory
  boundaries and Tier-1 Boot with all six OTA compile/runtime gates equal to
  zero.
- The N16 Wi-Fi profiles intentionally have `CONFIG_BK7258_PSRAM` disabled.
  No new PSRAM board run is claimed; the separate, previously board-verified
  N14/N15 PSRAM profile and ownership boundary were not modified.

This record closes N16. It does not claim SoftAP, Wi-Fi teardown/warm
recovery, complete power removal, power-save, performance tuning, legacy-SDK
compatibility or network OTA.
