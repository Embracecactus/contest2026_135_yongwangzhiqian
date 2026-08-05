# N16 Wi-Fi malloc compatibility verification

Date: 2026-08-05

## Outcome

The official Beken v3.1.1.9 CP Wi-Fi archive has a hidden first-use contract:
selected `malloc()` results must be zero before station management consumes
embedded list state. NuttX correctly provides no such `malloc()` guarantee.

The accepted board compatibility layer keeps link-time `--wrap=malloc` for
immutable archive calls, but clears memory only when the scope is active and
the calling PID is the thread executing `bk_wifi_init()`. Other CP threads,
the AP image and Wi-Fi-disabled profiles retain normal allocation behavior.

## Root-cause evidence

Temporary observation-only firmware deliberately stopped clearing allocations
inside the Wi-Fi initialization window:

- loader reboot: 57/57 observed allocations contained nonzero data; 23,454
  bytes were allocated and 1,388 bytes were nonzero. The path eventually
  reached NSH, showing that loader residue can mask the defect;
- COM7 RTS: the first 7/7 allocations contained nonzero data; 6,414 bytes were
  allocated and 455 bytes were nonzero;
- the RTS path then HardFaulted with stacked PC `0xaaaaaaaa` and LR
  `0x02061127`, symbolized to `sta_mgmt_entry_init()` in the exact v3.1.1.9
  SDK. Disassembly showed the nonzero embedded list head selected the stale
  release path.

Temporary counters and application-entry register prints were removed after
capture. The official SDK and official NuttX/apps sources were not modified.

Evidence directories outside the repository:

- `/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260805-211450`
- `/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260805-212350`

## Scoped implementation verification

- Full `cp_nsh_wifi + ap_smp_wifi` dual build passed SDK checksum, partition,
  factory-layout, SDK-wrapper ELF and RPTUN-layout gates.
- CP ELF disassembly shows `__wrap_malloc()` tests both the active flag and
  `g_bk7258_wifi_malloc_owner_pid` against the current NuttX PID before its
  `memset()` call.
- Sparse flash preserved LittleFS, slot B and calibration ranges.
- Loader reboot reached NSH.
- COM7 RTS reached NSH; the former station-management HardFault did not recur.
- Post-RTS status: AP `READY`, RPTUN `CONNECTED`, supervisor `HEALTHY`, AP SMP
  `PASSED`, and `bkwifi status=0`.
- RPMsg idle run passed for both AP logical CPUs: 20 sent, 20 received and zero
  errors per CPU.

Evidence:

- `/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260805-215150`
- `/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260805-215300`
- `/tmp/bk7258-wifi-owner-scope-health/session.json`

This does not claim STA association, DHCP or Wi-Fi socket data-plane closure.

## Future diagnostic rule

Do not broaden the Wi-Fi owner scope when another immutable component fails.
First use temporary observation-only telemetry around that component's own
initialization, record caller/owner/size/nonzero evidence, symbolize the first
fault, and add a separately named component scope only if the same hidden
contract is proven. Remove the telemetry after diagnosis.
