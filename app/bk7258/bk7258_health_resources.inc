/* SPDX-License-Identifier: Apache-2.0 */

/* Explicit console observation on the existing health worker. No collection
 * timer, allocation, sensor read, storage mount or changed admission state.
 */

#include <malloc.h>
#include <nuttx/arch.h>
#include <nuttx/sched.h>
#ifdef CONFIG_BK7258_MOTION_SERVICE
#  include "bk7258_motion_service.h"
#endif
#ifdef CONFIG_BK7258_PSRAM
#  include <arch/chip/bk7258_psram.h>
#endif
#ifdef CONFIG_BK7258_USBCDC
#  include <arch/chip/bk7258_usbcdc.h>
#endif
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
#  include "bk7258_display_service.h"
#endif
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
#  include "bk7258_haptic_service.h"
#endif

struct bkhealth_stacks_s
{
  size_t bytes;
  size_t used;
  size_t least_free;
  unsigned int threads;
  int least_pid;
};

static void bkhealth_stack_observe(struct tcb_s *tcb, void *context)
{
  struct bkhealth_stacks_s *stacks = context;
#ifdef CONFIG_STACK_COLORATION
  size_t used;
  size_t available;
#endif

  stacks->threads++;
  stacks->bytes += tcb->adj_stack_size;
#ifdef CONFIG_STACK_COLORATION
  used = up_check_tcbstack(tcb, tcb->adj_stack_size);
  available = used <= tcb->adj_stack_size ? tcb->adj_stack_size - used : 0;

  stacks->used += used;
  if (available < stacks->least_free)
    {
      stacks->least_free = available;
      stacks->least_pid = tcb->pid;
    }
#endif
}

static void bkhealth_resources(struct bkhealth_rpc_response_s *response)
{
  struct mallinfo heap = mallinfo();
  struct bkhealth_stacks_s stacks = { .least_free = SIZE_MAX };
#ifdef CONFIG_BK7258_MOTION_SERVICE
  struct bkmotion_metrics_s motion;
#endif
  struct timespec begin = {0};
  struct timespec end = {0};
#ifdef CONFIG_BK7258_PSRAM
  struct bk7258_psram_info_s psram;
#endif
#ifdef CONFIG_BK7258_USBCDC
  struct bk7258_usbcdc_snapshot_s usb;
#endif
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
  struct bkdisplay_service_status_s display;
#endif
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
  struct bkhaptic_product_status_s haptic;
#endif

  clock_gettime(CLOCK_MONOTONIC, &begin);
  nxsched_foreach(bkhealth_stack_observe, &stacks);
  clock_gettime(CLOCK_MONOTONIC, &end);
  response->reserved[0] = heap.uordblks;
  response->reserved[1] = heap.fordblks;
  syslog(LOG_INFO, "BKRESOURCE AP heap_total=%u used=%u free=%u "
         "peak=%u largest_free=%u\n", heap.arena, heap.uordblks,
         heap.fordblks, heap.usmblks, heap.mxordblk);
  syslog(LOG_INFO, "BKRESOURCE AP threads=%u stack_bytes=%lu "
         "stack_used=%lu least_free=%lu least_pid=%d stack_valid=%d "
         "scan_us=%lld\n", stacks.threads, (unsigned long)stacks.bytes,
         (unsigned long)stacks.used,
         (unsigned long)(stacks.least_free == SIZE_MAX ? 0 : stacks.least_free),
         stacks.least_pid, stacks.least_free != SIZE_MAX,
         (long long)(end.tv_sec - begin.tv_sec) * 1000000 +
         (end.tv_nsec - begin.tv_nsec) / 1000);
#ifdef CONFIG_BK7258_PSRAM
  if (bk7258_psram_get_info(&psram) == 0)
    syslog(LOG_INFO, "BKRESOURCE AP psram_ready=%lu heap_total=%lu "
           "heap_free=%lu minimum_free=%lu\n", (unsigned long)psram.ready,
           (unsigned long)psram.heap_total, (unsigned long)psram.heap_free,
           (unsigned long)psram.heap_minimum_free);
#endif
#ifdef CONFIG_BK7258_USBCDC
  if (bk7258_usbcdc_snapshot(&usb) == 0)
    syslog(LOG_INFO, "BKRESOURCE CDC flags=%lu rx_bytes=%lu tx_bytes=%lu "
           "rx_queued=%lu tx_queued=%lu rx_error=%ld\n",
           (unsigned long)usb.flags, (unsigned long)usb.rx_bytes,
           (unsigned long)usb.tx_bytes, (unsigned long)usb.rx_queued,
           (unsigned long)usb.tx_queued, (long)usb.rx_error);
#endif
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
  if (bk7258_display_get_status(&display) == 0)
    syslog(LOG_INFO, "BKRESOURCE DISPLAY state=%u overlay=%u error=%d "
           "sequence=%lu revision=%lu\n", display.state, display.overlay,
           display.last_error, (unsigned long)display.render_sequence,
           (unsigned long)display.pack_revision);
#endif
#ifdef CONFIG_BK7258_MOTION_SERVICE
  if (bk7258_motion_service_metrics(&motion) == 0)
    syslog(LOG_INFO, "BKRESOURCE MOTION collections=%lu open_us=%lu "
           "read_us=%lu close_us=%lu total_us=%lu\n",
           (unsigned long)motion.collections, (unsigned long)motion.open_us,
           (unsigned long)motion.read_us, (unsigned long)motion.close_us,
           (unsigned long)motion.total_us);
#endif
#ifdef CONFIG_BK7258_HAPTIC_SERVICE
  if (bkhaptic_service_product_status(&haptic) == 0)
    syslog(LOG_INFO, "BKRESOURCE HAPTIC sequence=%lu completed=%lu "
           "active=%d result=%d fault=%d\n", (unsigned long)haptic.sequence,
           (unsigned long)haptic.completed, haptic.active,
           haptic.result, haptic.fault);
#endif
}
