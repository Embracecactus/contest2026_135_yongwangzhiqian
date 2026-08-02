/****************************************************************************
 * arch/arm/include/bk7258/bk7258_rptun.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 CP/AP N9 shared-memory and mailbox ABI.
 ****************************************************************************/

#ifndef __ARCH_ARM_INCLUDE_BK7258_BK7258_RPTUN_H
#define __ARCH_ARM_INCLUDE_BK7258_BK7258_RPTUN_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <assert.h>
#include <stdint.h>

#include <arch/chip/bk7258_amp.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_RPTUN_CONTROL_MAGIC        0x54505242u /* "BRPT" */
#define BK7258_RPTUN_CONTROL_VERSION      1u
#define BK7258_RPTUN_CONTROL_ALIGN        64u
#define BK7258_RPTUN_RESOURCE_ALIGN       8u
#define BK7258_RPTUN_CARVEOUT_ALIGN       64u

/* These sizes are frozen against the current NuttX/OpenAMP checkout by
 * verify_bk7258_rptun_layout.py.  When CONFIG_RPTUN is enabled, the lower
 * half also static-asserts sizeof(struct rptun_rsc_s) and vring_size().
 */

#define BK7258_RPTUN_CONTROL_OFFSET       0x0000u
#define BK7258_RPTUN_CONTROL_SIZE         0x0040u
#define BK7258_RPTUN_RESOURCE_OFFSET      0x0040u
#define BK7258_RPTUN_RESOURCE_SIZE        0x0108u
#define BK7258_RPTUN_CARVEOUT_OFFSET      0x0180u
#define BK7258_RPTUN_CARVEOUT_SIZE        \
  (BK7258_RPTUN_SHMEM_SIZE - BK7258_RPTUN_CARVEOUT_OFFSET)

#define BK7258_RPTUN_VRING_COUNT          2u
#define BK7258_RPTUN_VRING_NUM            8u
#define BK7258_RPTUN_VRING_ALIGN          8u
#define BK7258_RPTUN_VRING_RAW_SIZE       222u
#define BK7258_RPTUN_VRING_SPAN           224u
#define BK7258_RPTUN_BUFFER_SIZE          512u
#define BK7258_RPTUN_BUFFER_BYTES         \
  (BK7258_RPTUN_VRING_COUNT * BK7258_RPTUN_VRING_NUM * \
   BK7258_RPTUN_BUFFER_SIZE)

/* AP-side cold-start progress bits.  These remain part of the shared control
 * ABI so a CP-side status command or an external debugger can distinguish an
 * SDK mailbox-init stall from an asynchronous RPTUN/OpenAMP connection wait.
 */

#define BK7258_RPTUN_FLAG_AP_MBOX_ENTER   (1u << 0)
#define BK7258_RPTUN_FLAG_AP_MBOX_READY   (1u << 1)
#define BK7258_RPTUN_FLAG_AP_RPTUN_ENTER  (1u << 2)
#define BK7258_RPTUN_FLAG_AP_RPTUN_READY  (1u << 3)
#define BK7258_RPTUN_FLAG_AP_READY        (1u << 4)
#define BK7258_RPTUN_FLAG_AP_CORE_READY   (1u << 5)
#define BK7258_RPTUN_FLAG_AP_TEST_ENTER   (1u << 6)
#define BK7258_RPTUN_FLAG_AP_TEST_READY   (1u << 7)
#define BK7258_RPTUN_FLAG_AP_MBOX_SEMS    (1u << 8)
#define BK7258_RPTUN_FLAG_AP_MBOX_THREAD  (1u << 9)
#define BK7258_RPTUN_FLAG_AP_MBOX_PINNED  (1u << 10)
#define BK7258_RPTUN_FLAG_AP_MBOX_INIT    (1u << 11)
#define BK7258_RPTUN_FLAG_AP_MBOX_OPEN    (1u << 12)
#define BK7258_RPTUN_FLAG_AP_MBOX_CBS     (1u << 13)
#define BK7258_RPTUN_FLAG_CONNECTED_ONCE  (1u << 14)
#define BK7258_RPTUN_MM_RESERVE           0x1000u
#define BK7258_RPTUN_MIN_CARVEOUT         \
  (BK7258_RPTUN_VRING_COUNT * BK7258_RPTUN_VRING_SPAN + \
   BK7258_RPTUN_BUFFER_BYTES + BK7258_RPTUN_MM_RESERVE)
#define BK7258_RPTUN_LAYOUT_SPARE         \
  (BK7258_RPTUN_CARVEOUT_SIZE - BK7258_RPTUN_MIN_CARVEOUT)

#define BK7258_RPTUN_CONTROL_ADDR         \
  (BK7258_RPTUN_SHMEM_BASE + BK7258_RPTUN_CONTROL_OFFSET)
#define BK7258_RPTUN_RESOURCE_ADDR        \
  (BK7258_RPTUN_SHMEM_BASE + BK7258_RPTUN_RESOURCE_OFFSET)
#define BK7258_RPTUN_CARVEOUT_ADDR        \
  (BK7258_RPTUN_SHMEM_BASE + BK7258_RPTUN_CARVEOUT_OFFSET)
#define BK7258_RPTUN_SHMEM_END            \
  (BK7258_RPTUN_SHMEM_BASE + BK7258_RPTUN_SHMEM_SIZE)

/* SDK MBOX0 runtime ABI.
 *
 * The raw MBOX0 envelope uses data[1] == 0 exclusively for AP-local SMP
 * commands.  CP/AP RPTUN uses the SDK mb_chnl wrapper on MB_CHNL_LOG:
 * data[1] == sizeof(mailbox_data_t), hdr.cmd identifies this protocol, and
 * param1..3 carry type/generation/value.  Vendor mailbox log forwarding is
 * deliberately not enabled; N9-F replaces it with syslog_rpmsg.
 */

#define BK7258_RPTUN_MBOX_COMMAND          0xb9u
#define BK7258_RPTUN_MBOX_DATA_LENGTH      16u
#define BK7258_RPTUN_MBOX_LOGICAL_INDEX    14u
#define BK7258_RPTUN_MBOX_PROBE_REPLY      (1u << 31)
#define BK7258_RPTUN_MBOX_PROBE_SEQUENCE   \
  (~BK7258_RPTUN_MBOX_PROBE_REPLY)

#define BK7258_RPTUN_NOTIFY_VRING0         (1u << 0)
#define BK7258_RPTUN_NOTIFY_VRING1         (1u << 1)
#define BK7258_RPTUN_NOTIFY_ALL            (1u << 31)

#define BK7258_RPMSG_TEST_RESULT_MAGIC      0x54535242u /* "BRST" */
#define BK7258_RPMSG_TEST_RESULT_VERSION    2u
#define BK7258_RPMSG_TEST_RESULT_SIZE       240u
#define BK7258_RPMSG_TEST_FRAME_SIZE        \
  (BK7258_RPTUN_BUFFER_SIZE - 16u)
#define BK7258_RPMSG_TEST_WIRE_HEADER_SIZE  32u
#define BK7258_RPMSG_TEST_MAX_PAYLOAD       \
  (BK7258_RPMSG_TEST_FRAME_SIZE - BK7258_RPMSG_TEST_WIRE_HEADER_SIZE)
#define BK7258_RPMSG_TEST_MAX_COUNT         1000u
#define BK7258_RPMSG_TEST_FLAG_CPU0_LOAD    (1u << 0)
#define BK7258_RPMSG_TEST_FLAG_SYSLOG_PROBE (1u << 1)
#define BK7258_RPMSG_TEST_VALID_FLAGS        \
  (BK7258_RPMSG_TEST_FLAG_CPU0_LOAD |       \
   BK7258_RPMSG_TEST_FLAG_SYSLOG_PROBE)

#define BK7258_RPMSGFS_TEST_RESULT_MAGIC     0x53465242u /* "BRFS" */
#define BK7258_RPMSGFS_TEST_RESULT_VERSION   1u
#define BK7258_RPMSGFS_TEST_RESULT_SIZE      96u
#define BK7258_RPMSGFS_TEST_MAX_PAYLOAD      1024u
#define BK7258_RPMSGFS_TEST_MAX_ITERATIONS   100u

/* Wrapper-only diagnostics distinguish pthread attribute/create failures
 * during permanent worker setup from a per-run semaphore dispatch failure.
 * Target and stage remain numeric in BRPT output so automated soak parsers
 * can gate on them without locale-sensitive strings.
 */

#define BK7258_RPMSG_TEST_SPAWN_TARGET_NONE    0u
#define BK7258_RPMSG_TEST_SPAWN_TARGET_LOAD    1u
#define BK7258_RPMSG_TEST_SPAWN_TARGET_CPU0    2u
#define BK7258_RPMSG_TEST_SPAWN_TARGET_CPU1    3u

#define BK7258_RPMSG_TEST_SPAWN_STAGE_NONE     0u
#define BK7258_RPMSG_TEST_SPAWN_STAGE_ATTR     1u
#define BK7258_RPMSG_TEST_SPAWN_STAGE_CREATE   2u
#define BK7258_RPMSG_TEST_SPAWN_STAGE_DISPATCH 3u

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_rptun_control_state_e
{
  BK7258_RPTUN_STATE_OFFLINE = 0,
  BK7258_RPTUN_STATE_PREPARING,
  BK7258_RPTUN_STATE_TABLE_READY,
  BK7258_RPTUN_STATE_CONNECTING,
  BK7258_RPTUN_STATE_CONNECTED,
  BK7258_RPTUN_STATE_QUIESCING,
  BK7258_RPTUN_STATE_FAULTED
};

enum bk7258_rptun_mbox_type_e
{
  BK7258_RPTUN_MBOX_INVALID = 0,
  BK7258_RPTUN_MBOX_LIFECYCLE,
  BK7258_RPTUN_MBOX_NOTIFY,
  BK7258_RPTUN_MBOX_HEARTBEAT,
  BK7258_RPTUN_MBOX_PROBE
};

struct bk7258_rptun_control_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t state;
  uint32_t flags;
  uint32_t error;
  uint32_t resource_crc32;
  uint32_t cp_to_ap_pending;
  uint32_t ap_to_cp_pending;
  uint32_t cp_heartbeat;
  uint32_t ap_heartbeat;
  uint32_t cp_epoch;
  uint32_t ap_epoch;
  uint32_t cp_rx_sequence;
  uint32_t ap_rx_sequence;
};

struct bk7258_rpmsg_test_cpu_result_s
{
  uint32_t sender_cpu;
  uint32_t callback_cpu_mask;
  uint32_t sent;
  uint32_t received;
  uint32_t errors;
  uint32_t min_cycles;
  uint32_t p50_cycles;
  uint32_t p95_cycles;
  uint32_t p99_cycles;
  uint32_t max_cycles;
  uint64_t total_cycles;
};

struct bk7258_rpmsg_test_heap_result_s
{
  uint32_t arena;
  uint32_t allocated_blocks;
  uint32_t free_blocks;
  uint32_t largest_free;
  uint32_t allocated_bytes;
  uint32_t free_bytes;
};

struct bk7258_rpmsg_test_result_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t run_id;
  uint32_t count;
  uint32_t payload_size;
  uint32_t frame_size;
  uint32_t flags;
  uint32_t frequency;
  int32_t status;
  uint32_t controller_cpu;
  struct bk7258_rpmsg_test_cpu_result_s cpu[2];
  struct bk7258_rpmsg_test_heap_result_s heap_start;
  struct bk7258_rpmsg_test_heap_result_s heap_after_spawn;
  struct bk7258_rpmsg_test_heap_result_s heap_report;
  int32_t spawn_status;
  uint32_t spawn_target;
  uint32_t spawn_stage;
  uint32_t workers_expected;
  uint32_t workers_done;
};

enum bk7258_rpmsgfs_test_step_e
{
  BK7258_RPMSGFS_TEST_STEP_NONE = 0,
  BK7258_RPMSGFS_TEST_STEP_GENERATION,
  BK7258_RPMSGFS_TEST_STEP_CLEANUP,
  BK7258_RPMSGFS_TEST_STEP_MKDIR,
  BK7258_RPMSGFS_TEST_STEP_OPEN_WRITE,
  BK7258_RPMSGFS_TEST_STEP_WRITE,
  BK7258_RPMSGFS_TEST_STEP_SYNC,
  BK7258_RPMSGFS_TEST_STEP_STAT,
  BK7258_RPMSGFS_TEST_STEP_OPEN_READ,
  BK7258_RPMSGFS_TEST_STEP_SEEK,
  BK7258_RPMSGFS_TEST_STEP_READ,
  BK7258_RPMSGFS_TEST_STEP_VERIFY,
  BK7258_RPMSGFS_TEST_STEP_RENAME,
  BK7258_RPMSGFS_TEST_STEP_OPENDIR,
  BK7258_RPMSGFS_TEST_STEP_READDIR,
  BK7258_RPMSGFS_TEST_STEP_UNLINK,
  BK7258_RPMSGFS_TEST_STEP_RMDIR,
  BK7258_RPMSGFS_TEST_STEP_REPORT
};

struct bk7258_rpmsgfs_test_result_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t sequence;
  uint32_t iterations_requested;
  uint32_t iterations_completed;
  uint32_t payload_size;
  int32_t  status;
  uint32_t step;
  uint32_t worker_cpu;
  uint32_t bytes_written;
  uint32_t bytes_read;
  uint32_t expected_checksum;
  uint32_t actual_checksum;
  uint32_t dir_entries;
  uint32_t heap_before_used;
  uint32_t heap_before_free;
  uint32_t heap_before_largest;
  uint32_t heap_after_used;
  uint32_t heap_after_free;
  uint32_t heap_after_largest;
  uint32_t reserved[2];
};

/****************************************************************************
 * Compile-time ABI Gates
 ****************************************************************************/

static_assert(sizeof(struct bk7258_rptun_control_s) ==
              BK7258_RPTUN_CONTROL_SIZE,
              "BK7258 RPTUN control ABI must remain 64 bytes");
static_assert((BK7258_RPTUN_RESOURCE_ADDR &
              (BK7258_RPTUN_RESOURCE_ALIGN - 1u)) == 0,
              "BK7258 RPTUN resource table is misaligned");
static_assert((BK7258_RPTUN_CARVEOUT_ADDR &
              (BK7258_RPTUN_CARVEOUT_ALIGN - 1u)) == 0,
              "BK7258 RPTUN carveout is misaligned");
static_assert(BK7258_RPTUN_RESOURCE_OFFSET + BK7258_RPTUN_RESOURCE_SIZE <=
              BK7258_RPTUN_CARVEOUT_OFFSET,
              "BK7258 RPTUN resource table overlaps carveout");
static_assert(BK7258_RPTUN_SHMEM_END == BK7258_SHARED_RAM_BASE,
              "BK7258 RPTUN region must end before telemetry");
static_assert(BK7258_RPTUN_CARVEOUT_SIZE >= BK7258_RPTUN_MIN_CARVEOUT,
              "BK7258 RPTUN carveout cannot hold vrings and buffers");
static_assert(BK7258_RPTUN_LAYOUT_SPARE >= 0x4000u,
              "BK7258 RPTUN carveout growth reserve is below 16 KiB");
static_assert(BK7258_RPMSG_TEST_WIRE_HEADER_SIZE +
              BK7258_RPMSG_TEST_MAX_PAYLOAD ==
              BK7258_RPMSG_TEST_FRAME_SIZE,
              "BK7258 RPMsg test frame ABI is inconsistent");
static_assert(sizeof(struct bk7258_rpmsg_test_heap_result_s) == 24u,
              "BK7258 RPMsg heap diagnostic ABI changed");
static_assert(sizeof(struct bk7258_rpmsg_test_result_s) ==
              BK7258_RPMSG_TEST_RESULT_SIZE,
              "BK7258 RPMsg result diagnostic ABI changed");
static_assert(sizeof(struct bk7258_rpmsg_test_result_s) <=
              BK7258_RPMSG_TEST_MAX_PAYLOAD,
              "BK7258 RPMsg test result cannot fit one frame");
static_assert(sizeof(struct bk7258_rpmsgfs_test_result_s) ==
              BK7258_RPMSGFS_TEST_RESULT_SIZE,
              "BK7258 RPMsgFS test result diagnostic ABI changed");
static_assert(sizeof(struct bk7258_rpmsgfs_test_result_s) + 32u <=
              BK7258_RPMSG_TEST_FRAME_SIZE,
              "BK7258 RPMsgFS report cannot fit one RPMsg frame");

/****************************************************************************
 * Public Inline Functions
 ****************************************************************************/

static inline volatile struct bk7258_rptun_control_s *
bk7258_rptun_control(void)
{
  return (volatile struct bk7258_rptun_control_s *)
    BK7258_RPTUN_CONTROL_ADDR;
}

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_RPTUN
void bk7258_rptun_mark_connected(void);
#endif

#ifdef CONFIG_BK7258_RPTUN_MBOX
int bk7258_rptun_mbox_probe(uint32_t count, uint32_t timeout_ms);
#endif

#if defined(CONFIG_BK7258_RPMSG_TEST) && !defined(CONFIG_BK7258_AP_CORE)
int bk7258_rpmsg_test_run(uint32_t count, uint32_t payload_size,
                          uint32_t flags, uint32_t timeout_ms,
                          struct bk7258_rpmsg_test_result_s *result);
#endif

#if defined(CONFIG_BK7258_RPMSGFS_TEST) && !defined(CONFIG_BK7258_AP_CORE)
int bk7258_rpmsgfs_test_run(uint32_t iterations, uint32_t payload_size,
                            uint32_t timeout_ms,
                            struct bk7258_rpmsgfs_test_result_s *result);
#endif

#endif /* __ARCH_ARM_INCLUDE_BK7258_BK7258_RPTUN_H */
