/****************************************************************************
 * chips/bk7258/ap/bk7258_ap_smp_diag.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Diagnostic-only AP SMP exclusive-access stress.  Both AP logical CPUs run
 * atomic-add and recursive-lock loops against one object per address class
 * (official lock window, ordinary SRAM 0x2806xxxx, ordinary SRAM 0x2809xxxx
 * and PSRAM) while optional RPMsg and DMA traffic runs on CPU0.  Each CPU
 * also samples its own NVIC IPR, SHPR, BASEPRI and PRIMASK, inside and
 * outside up_irq_save(), without changing the interrupt-masking mechanism.
 * Results are counters only; nothing here is enabled in a product profile.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AP_SMP_DIAG

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/atomic.h>
#include <nuttx/fs/fs.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/sched.h>
#include <nuttx/spinlock.h>
#ifdef CONFIG_BK7258_AP_SMP_DIAG_RPMSG_LOAD
#  include <nuttx/rpmsg/rpmsg.h>
#endif

#include <arch/chip/bk7258_ap_smp_diag.h>
#include <arch/chip/bk7258_os_adapt.h>
#include <arch/chip/bk7258_smp_placement.h>
#ifdef CONFIG_BK7258_PSRAM
#  include <arch/chip/bk7258_psram.h>
#endif
#ifdef CONFIG_BK7258_AP_SMP_DIAG_DMA_LOAD
#  include <arch/chip/bk7258_dma.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if CONFIG_SMP_NCPUS != 2
#  error "BK7258 SMP diagnostics support exactly two AP logical CPUs"
#endif

#define DIAG_NVIC_IPR_BASE      0xe000e400u
#define DIAG_SCB_SHPR_BASE      0xe000ed18u
#define DIAG_WINDOW_2806_LO     0x28060000u
#define DIAG_WINDOW_2806_HI     0x28070000u
#define DIAG_WINDOW_2809_LO     0x28090000u
#define DIAG_WINDOW_2809_HI     0x280a0000u
#define DIAG_PROBE_HOLD         32
#define DIAG_ABORT_MASK         0xffffu
#define DIAG_MAX_OPS            1000000000ull
#define DIAG_DMA_BYTES          4096u
#define DIAG_RPMSG_CPU          "cp"
#define DIAG_RPMSG_PING_CMD     0x3     /* ACK | CHECK */
#define DIAG_WITNESS_PER_CPU \
  (CONFIG_BK7258_AP_SMP_DIAG_WITNESS_ENTRIES / 2)
#define DIAG_FAILURES_PER_CPU   (BK7258_SMP_DIAG_FAILURE_ENTRIES / 2)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct diag_obj_s
{
  atomic_t counter;                    /* atomic-add target */
  rspinlock_t lock;                    /* recursive-lock target */
  volatile uint32_t owner;             /* logical CPU + 1 inside the lock */
  volatile uint32_t release_sequence;  /* advanced at each release */
  volatile uint32_t plain;             /* non-atomic update under the lock */
};

struct diag_worker_s
{
  FAR struct diag_obj_s *obj;
  uint64_t ops;
  uint64_t atomic_ops;
  uint64_t lock_ops;
  uint32_t cpu;
  uint8_t address_class;
  uint32_t atomic_failures;
  uint32_t owner_violations;
  uint32_t sequence_failures;
  uint32_t lockword_failures;
  struct bk7258_smp_diag_cpu_regs_s regs;
  int result;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* The rendezvous and abort words live in the official lock window so the
 * control path never depends on the address class under test.  Witness
 * rings are per CPU and written with plain stores so recording does not add
 * another exclusive access to the loop being measured.
 */

static struct diag_obj_s g_diag_safe_obj BK7258_SMP_LOCK_BSS;
static volatile uint32_t g_diag_rendezvous BK7258_SMP_LOCK_BSS;
static volatile uint32_t g_diag_abort BK7258_SMP_LOCK_BSS;
static volatile uint32_t g_diag_stop BK7258_SMP_LOCK_BSS;
static struct diag_obj_s g_diag_bss_obj;
static struct bk7258_smp_diag_witness_s
  g_diag_witness[2][DIAG_WITNESS_PER_CPU];
static uint32_t g_diag_witness_head[2];
static struct bk7258_smp_diag_witness_s
  g_diag_failures[2][DIAG_FAILURES_PER_CPU];
static uint32_t g_diag_failure_count[2];
static mutex_t g_diag_run_mutex = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint32_t diag_read_primask(void)
{
  uint32_t value;

  __asm volatile ("mrs %0, primask" : "=r" (value));
  return value;
}

static inline uint32_t diag_read_basepri(void)
{
  uint32_t value;

  __asm volatile ("mrs %0, basepri" : "=r" (value));
  return value;
}

static void diag_sample_regs(FAR struct bk7258_smp_diag_cpu_regs_s *regs)
{
  irqstate_t flags;
  uint32_t index;

  regs->cpu = up_cpu_index();
  regs->primask = diag_read_primask();
  regs->basepri = diag_read_basepri();

  /* Record which mechanism the current NuttX build uses for an IRQ-masked
   * critical section.  The mechanism itself is deliberately not changed.
   */

  flags = up_irq_save();
  regs->irqsave_primask = diag_read_primask();
  regs->irqsave_basepri = diag_read_basepri();
  up_irq_restore(flags);

  for (index = 0; index < BK7258_SMP_DIAG_SHPR_WORDS; index++)
    {
      regs->shpr[index] =
        *(FAR volatile uint32_t *)(uintptr_t)(DIAG_SCB_SHPR_BASE +
                                              index * 4u);
    }

  for (index = 0; index < BK7258_SMP_DIAG_NVIC_IPR_WORDS; index++)
    {
      regs->ipr[index] =
        *(FAR volatile uint32_t *)(uintptr_t)(DIAG_NVIC_IPR_BASE +
                                              index * 4u);
    }

  regs->valid = 1u;
}

static void diag_fill(FAR struct bk7258_smp_diag_witness_s *entry,
                      uint32_t cpu, uint8_t address_class, uint16_t kind,
                      FAR const volatile void *address, uint32_t observed,
                      uint32_t release_sequence)
{
  entry->cpu = (uint8_t)cpu;
  entry->address_class = address_class;
  entry->kind = kind;
  entry->address = (uint32_t)(uintptr_t)address;
  entry->observed = observed;
  entry->release_sequence = release_sequence;
}

static void diag_record(uint32_t cpu, uint8_t address_class,
                        FAR const volatile void *address, uint32_t observed,
                        uint32_t release_sequence)
{
  uint32_t index = g_diag_witness_head[cpu]++;

  diag_fill(&g_diag_witness[cpu][index % DIAG_WITNESS_PER_CPU], cpu,
            address_class, BK7258_SMP_DIAG_KIND_LOCK, address, observed,
            release_sequence);
}

static void diag_fail(uint32_t cpu, uint8_t address_class, uint16_t kind,
                      FAR const volatile void *address, uint32_t observed,
                      uint32_t release_sequence)
{
  uint32_t index = g_diag_failure_count[cpu]++;

  if (index < DIAG_FAILURES_PER_CPU)
    {
      diag_fill(&g_diag_failures[cpu][index], cpu, address_class, kind,
                address, observed, release_sequence);
    }
}

static void diag_rendezvous(uint32_t target)
{
  __atomic_fetch_add(&g_diag_rendezvous, 1u, __ATOMIC_ACQ_REL);
  while (__atomic_load_n(&g_diag_rendezvous, __ATOMIC_ACQUIRE) < target &&
         __atomic_load_n(&g_diag_abort, __ATOMIC_ACQUIRE) == 0u)
    {
    }
}

static FAR void *diag_worker(FAR void *argument)
{
  FAR struct diag_worker_s *worker = argument;
  FAR struct diag_obj_s *obj = worker->obj;
  uint32_t self = worker->cpu + 1u;
  int32_t last_old = -1;
  uint64_t index;

  if (up_cpu_index() != (int)worker->cpu)
    {
      worker->result = -EXDEV;
      __atomic_store_n(&g_diag_abort, 1u, __ATOMIC_RELEASE);
      return NULL;
    }

  diag_sample_regs(&worker->regs);

  /* Phase 1: lost-update and monotonic-old-value check on one word. */

  diag_rendezvous(2u);
  for (index = 0; index < worker->ops; index++)
    {
      int32_t old = atomic_add(&obj->counter, 1);

      if (old <= last_old)
        {
          worker->atomic_failures++;
          diag_fail(worker->cpu, worker->address_class,
                    BK7258_SMP_DIAG_KIND_ATOMIC, &obj->counter,
                    (uint32_t)old, (uint32_t)last_old);
        }

      last_old = old;
      if ((index & DIAG_ABORT_MASK) == 0u &&
          __atomic_load_n(&g_diag_abort, __ATOMIC_ACQUIRE) != 0u)
        {
          break;
        }
    }

  worker->atomic_ops = index;

  /* Phase 2: nested rspinlock acquisition.  "observed" is the lock word
   * sampled immediately before the outer acquiring CAS.
   */

  diag_rendezvous(4u);
  for (index = 0; index < worker->ops; index++)
    {
      uint32_t observed = (uint32_t)atomic_read(&obj->lock.val);
      irqstate_t outer = rspin_lock_irqsave(&obj->lock);
      irqstate_t inner = rspin_lock_irqsave(&obj->lock);
      uint32_t previous = obj->owner;
      uint32_t sequence = obj->release_sequence;

      if (obj->lock.owner != self || obj->lock.count != 2u)
        {
          worker->lockword_failures++;
          diag_fail(worker->cpu, worker->address_class,
                    BK7258_SMP_DIAG_KIND_LOCKWORD, &obj->lock,
                    (uint32_t)atomic_read(&obj->lock.val), sequence);
        }

      if (previous != 0u)
        {
          worker->owner_violations++;
          diag_fail(worker->cpu, worker->address_class,
                    BK7258_SMP_DIAG_KIND_OWNER, &obj->lock, previous,
                    sequence);
        }

      obj->owner = self;
      diag_record(worker->cpu, worker->address_class, &obj->lock,
                  observed, sequence);
      if (obj->plain != sequence)
        {
          worker->sequence_failures++;
          diag_fail(worker->cpu, worker->address_class,
                    BK7258_SMP_DIAG_KIND_SEQUENCE, &obj->lock, obj->plain,
                    sequence);
        }

      obj->plain = sequence + 1u;
      if (obj->owner != self)
        {
          worker->owner_violations++;
          diag_fail(worker->cpu, worker->address_class,
                    BK7258_SMP_DIAG_KIND_OWNER, &obj->lock, obj->owner,
                    sequence);
        }

      obj->owner = 0u;
      obj->release_sequence = sequence + 1u;
      rspin_unlock_irqrestore(&obj->lock, inner);
      rspin_unlock_irqrestore(&obj->lock, outer);

      if ((index & DIAG_ABORT_MASK) == 0u &&
          __atomic_load_n(&g_diag_abort, __ATOMIC_ACQUIRE) != 0u)
        {
          break;
        }
    }

  worker->lock_ops = index;
  worker->result = OK;
  return NULL;
}

static int diag_thread_create(FAR pthread_t *thread, int cpu, int priority,
                              FAR void *(*entry)(FAR void *),
                              FAR void *argument)
{
  struct sched_param param;
  pthread_attr_t attr;
  cpu_set_t cpuset;
  int ret;

  ret = pthread_attr_init(&attr);
  if (ret != 0)
    {
      return -ret;
    }

  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  memset(&param, 0, sizeof(param));
  param.sched_priority = priority;
  ret = pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(&attr,
                                      CONFIG_BK7258_AP_SMP_DIAG_STACKSIZE);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setschedparam(&attr, &param);
    }

  if (ret == 0)
    {
      ret = pthread_create(thread, &attr, entry, argument);
    }

  (void)pthread_attr_destroy(&attr);
  return -ret;
}

static int diag_run_class(uint8_t address_class,
                          FAR struct diag_obj_s *obj, uint64_t ops,
                          FAR struct bk7258_smp_diag_result_s *result)
{
  FAR struct bk7258_smp_diag_class_result_s *cls =
    &result->cls[address_class];
  struct diag_worker_s workers[2];
  pthread_t threads[2];
  bool created[2] =
  {
    false, false
  };

  uint64_t expected;
  int ret = OK;
  int cpu;

  memset(obj, 0, sizeof(*obj));
  memset(workers, 0, sizeof(workers));
  __atomic_store_n(&g_diag_rendezvous, 0u, __ATOMIC_RELEASE);
  __atomic_store_n(&g_diag_abort, 0u, __ATOMIC_RELEASE);

  for (cpu = 1; cpu >= 0; cpu--)
    {
      workers[cpu].obj = obj;
      workers[cpu].ops = ops;
      workers[cpu].cpu = (uint32_t)cpu;
      workers[cpu].address_class = address_class;
      workers[cpu].result = -EINPROGRESS;
      ret = diag_thread_create(&threads[cpu], cpu,
                               CONFIG_BK7258_AP_SMP_DIAG_PRIORITY,
                               diag_worker, &workers[cpu]);
      if (ret < 0)
        {
          __atomic_store_n(&g_diag_abort, 1u, __ATOMIC_RELEASE);
          break;
        }

      created[cpu] = true;
    }

  for (cpu = 0; cpu < 2; cpu++)
    {
      if (created[cpu])
        {
          (void)pthread_join(threads[cpu], NULL);
        }
    }

  for (cpu = 0; cpu < 2; cpu++)
    {
      cls->atomic_ops += workers[cpu].atomic_ops;
      cls->lock_ops += workers[cpu].lock_ops;
      cls->atomic_failures += workers[cpu].atomic_failures;
      cls->owner_violations += workers[cpu].owner_violations;
      cls->sequence_failures += workers[cpu].sequence_failures;
      cls->lockword_failures += workers[cpu].lockword_failures;
      if (workers[cpu].regs.valid != 0u)
        {
          memcpy(&result->cpu[cpu], &workers[cpu].regs,
                 sizeof(result->cpu[cpu]));
        }

      if (ret == OK && workers[cpu].result < 0)
        {
          ret = workers[cpu].result;
        }
    }

  /* Any difference between completed operations and the final shared
   * values is a lost update that no per-operation check could observe.
   */

  expected = cls->atomic_ops;
  if ((uint64_t)(uint32_t)atomic_read(&obj->counter) != expected)
    {
      cls->atomic_failures++;
    }

  expected = cls->lock_ops;
  if ((uint64_t)obj->plain != expected ||
      (uint64_t)obj->release_sequence != expected)
    {
      cls->lock_failures++;
    }

  cls->status = ret;
  return ret;
}

/* Find a heap block whose object lies entirely in [low, high).  Probing
 * blocks are held only until the search completes, then released.
 */

static FAR void *diag_window_alloc(uintptr_t low, uintptr_t high)
{
  static const size_t alignments[] =
  {
    0x10000u, 0x1000u, 0x100u
  };

  FAR void *held[DIAG_PROBE_HOLD];
  FAR void *found = NULL;
  int count = 0;
  unsigned int step;

  if ((uintptr_t)&g_diag_bss_obj >= low &&
      (uintptr_t)(&g_diag_bss_obj + 1) <= high)
    {
      return &g_diag_bss_obj;
    }

  for (step = 0; step < sizeof(alignments) / sizeof(alignments[0]) &&
                 found == NULL; step++)
    {
      while (count < DIAG_PROBE_HOLD)
        {
          FAR void *probe = kmm_memalign(alignments[step],
                                         sizeof(struct diag_obj_s));

          if (probe == NULL)
            {
              break;
            }

          if ((uintptr_t)probe >= low &&
              (uintptr_t)probe + sizeof(struct diag_obj_s) <= high)
            {
              found = probe;
              break;
            }

          held[count++] = probe;
        }
    }

  while (count > 0)
    {
      kmm_free(held[--count]);
    }

  return found;
}

static FAR struct diag_obj_s *diag_object(uint8_t address_class)
{
  switch (address_class)
    {
      case BK7258_SMP_DIAG_CLASS_SAFE:
        return &g_diag_safe_obj;

      case BK7258_SMP_DIAG_CLASS_SRAM_2806:
        return diag_window_alloc(DIAG_WINDOW_2806_LO, DIAG_WINDOW_2806_HI);

      case BK7258_SMP_DIAG_CLASS_SRAM_2809:
        return diag_window_alloc(DIAG_WINDOW_2809_LO, DIAG_WINDOW_2809_HI);

#ifdef CONFIG_BK7258_PSRAM
      case BK7258_SMP_DIAG_CLASS_PSRAM:
        {
          /* Deliberately place SMP atomic state in PSRAM: this class exists
           * only to measure the hazard the product placement rules avoid.
           */

          FAR void *object = bk7258_psram_malloc(sizeof(struct diag_obj_s));

          if (object != NULL && !bk7258_smp_addr_is_psram(object))
            {
              bk7258_psram_free(object);
              object = NULL;
            }

          return object;
        }
#endif

      default:
        return NULL;
    }
}

static void diag_object_release(uint8_t address_class,
                                FAR struct diag_obj_s *obj)
{
  if (obj == NULL || obj == &g_diag_safe_obj || obj == &g_diag_bss_obj)
    {
      return;
    }

#ifdef CONFIG_BK7258_PSRAM
  if (address_class == BK7258_SMP_DIAG_CLASS_PSRAM)
    {
      bk7258_psram_free(obj);
      return;
    }
#else
  (void)address_class;
#endif

  kmm_free(obj);
}

static void diag_audit_tcb(FAR struct tcb_s *tcb, FAR void *argument)
{
  FAR struct bk7258_smp_diag_result_s *result = argument;

  result->tcb_total++;
  if (bk7258_smp_addr_is_psram(tcb))
    {
      result->tcb_psram++;
    }

  if (tcb->group != NULL && bk7258_smp_addr_is_psram(tcb->group))
    {
      result->group_psram++;
    }

  /* A stack is not itself a lock, but kernel and RPMsg paths place
   * semaphores on the caller's stack.  Report it separately.
   */

  if (tcb->stack_alloc_ptr != NULL &&
      bk7258_smp_addr_is_psram(tcb->stack_alloc_ptr))
    {
      result->stack_psram++;
    }
}

#ifdef CONFIG_BK7258_AP_SMP_DIAG_DMA_LOAD
static FAR void *diag_dma_worker(FAR void *argument)
{
  FAR struct bk7258_smp_diag_result_s *result = argument;
  struct bk7258_dma_xfer_s xfer;
  struct file filep;
  FAR uint32_t *source;
  FAR uint32_t *target;
  uint32_t sequence = 0;
  uint32_t index;
  int ret;

  ret = file_open(&filep, BK7258_DMA_DEVPATH, O_RDWR);
  if (ret < 0)
    {
      result->dma_status = ret;
      return NULL;
    }

  source = kmm_memalign(32, DIAG_DMA_BYTES);
  target = kmm_memalign(32, DIAG_DMA_BYTES);
  if (source == NULL || target == NULL)
    {
      result->dma_status = -ENOMEM;
      goto out;
    }

  while (__atomic_load_n(&g_diag_stop, __ATOMIC_ACQUIRE) == 0u)
    {
      for (index = 0; index < DIAG_DMA_BYTES / 4u; index++)
        {
          source[index] = 0xa5000000u ^ (sequence << 12) ^ index;
        }

      memset(target, 0, DIAG_DMA_BYTES);
      xfer.src_addr = (uint32_t)(uintptr_t)source;
      xfer.dst_addr = (uint32_t)(uintptr_t)target;
      xfer.length = DIAG_DMA_BYTES;
      xfer.width = 32u;
      ret = file_ioctl(&filep, BKIOC_DMA_TRANSFER,
                       (unsigned long)(uintptr_t)&xfer);
      if (ret < 0 || memcmp(source, target, DIAG_DMA_BYTES) != 0)
        {
          result->dma_failures++;
          if (ret < 0)
            {
              result->dma_status = ret;
            }
        }

      result->dma_ops++;
      sequence++;
    }

out:
  kmm_free(source);
  kmm_free(target);
  file_close(&filep);
  return NULL;
}
#endif

#ifdef CONFIG_BK7258_AP_SMP_DIAG_RPMSG_LOAD
static FAR void *diag_rpmsg_worker(FAR void *argument)
{
  FAR struct bk7258_smp_diag_result_s *result = argument;

  while (__atomic_load_n(&g_diag_stop, __ATOMIC_ACQUIRE) == 0u)
    {
      struct rpmsg_ping_s ping;
      int ret;

      memset(&ping, 0, sizeof(ping));
      ping.times = 1;
      ping.len = 64;
      ping.cmd = DIAG_RPMSG_PING_CMD;
      ping.sleep = 0;
      ret = rpmsg_ioctl(DIAG_RPMSG_CPU, RPMSGIOC_PING,
                        (unsigned long)(uintptr_t)&ping);
      if (ret < 0)
        {
          result->rpmsg_failures++;
          result->rpmsg_status = ret;
        }

      result->rpmsg_ops++;
    }

  return NULL;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_ap_smp_diag_run(const struct bk7258_smp_diag_config_s *config,
                           struct bk7258_smp_diag_result_s *result)
{
  struct bk7258_os_sync_pool_stats_s pool;
  pthread_t loaders[2] =
  {
    0, 0
  };

  bool loader_created[2] =
  {
    false, false
  };

  uint64_t ops;
  uint8_t address_class;
  int ret;
  int index;

  if (config == NULL || result == NULL)
    {
      return -EINVAL;
    }

  ops = config->ops_per_class != 0u ? config->ops_per_class :
        (uint64_t)CONFIG_BK7258_AP_SMP_DIAG_OPS;
  if (ops > DIAG_MAX_OPS)
    {
      return -ERANGE;
    }

  ret = nxmutex_lock(&g_diag_run_mutex);
  if (ret < 0)
    {
      return ret;
    }

  memset(result, 0, sizeof(*result));
  result->version = BK7258_SMP_DIAG_VERSION;
  result->size = sizeof(*result);
  result->ops_per_class = ops;
  result->target_met = ops >= BK7258_SMP_DIAG_MIN_TARGET_OPS ? 1u : 0u;
  memset(g_diag_witness_head, 0, sizeof(g_diag_witness_head));
  memset(g_diag_failure_count, 0, sizeof(g_diag_failure_count));

  nxsched_foreach(diag_audit_tcb, result);
  if (bk7258_os_sync_pool_get_stats(&pool) == OK)
    {
      result->sync_psram_rejections = pool.psram_rejections;
    }

  __atomic_store_n(&g_diag_stop, 0u, __ATOMIC_RELEASE);
  result->dma_status = -ENOSYS;
  result->rpmsg_status = -ENOSYS;

  /* Load threads block on DMA/RPMsg completion, so one priority step above
   * the CPU0 stress worker lets them run without starving it.
   */

#ifdef CONFIG_BK7258_AP_SMP_DIAG_DMA_LOAD
  if ((config->load_mask & BK7258_SMP_DIAG_LOAD_DMA) != 0u)
    {
      result->dma_status = OK;
      loader_created[0] = diag_thread_create(
        &loaders[0], 0, CONFIG_BK7258_AP_SMP_DIAG_PRIORITY + 1,
        diag_dma_worker, result) == OK;
    }
#endif

#ifdef CONFIG_BK7258_AP_SMP_DIAG_RPMSG_LOAD
  if ((config->load_mask & BK7258_SMP_DIAG_LOAD_RPMSG) != 0u)
    {
      result->rpmsg_status = OK;
      loader_created[1] = diag_thread_create(
        &loaders[1], 0, CONFIG_BK7258_AP_SMP_DIAG_PRIORITY + 1,
        diag_rpmsg_worker, result) == OK;
    }
#endif

  for (address_class = 0; address_class < BK7258_SMP_DIAG_CLASS_COUNT;
       address_class++)
    {
      FAR struct diag_obj_s *obj;

      if ((config->class_mask & (1u << address_class)) == 0u)
        {
          result->cls[address_class].status = -ECANCELED;
          continue;
        }

      obj = diag_object(address_class);
      if (obj == NULL)
        {
          result->cls[address_class].status = -ENOENT;
          continue;
        }

      result->cls[address_class].address = (uint32_t)(uintptr_t)obj;
      (void)diag_run_class(address_class, obj, ops, result);
      diag_object_release(address_class, obj);
    }

  __atomic_store_n(&g_diag_stop, 1u, __ATOMIC_RELEASE);
  for (index = 0; index < 2; index++)
    {
      if (loader_created[index])
        {
          (void)pthread_join(loaders[index], NULL);
        }
    }

  result->witness_head = g_diag_witness_head[0] + g_diag_witness_head[1];
  result->failure_count = g_diag_failure_count[0] +
                          g_diag_failure_count[1];
  result->status = OK;
  for (address_class = 0; address_class < BK7258_SMP_DIAG_CLASS_COUNT;
       address_class++)
    {
      FAR const struct bk7258_smp_diag_class_result_s *cls =
        &result->cls[address_class];

      if (cls->status < 0 && cls->status != -ECANCELED &&
          cls->status != -ENOENT)
        {
          result->status = cls->status;
        }
      else if (cls->atomic_failures != 0u || cls->lock_failures != 0u ||
               cls->owner_violations != 0u ||
               cls->sequence_failures != 0u ||
               cls->lockword_failures != 0u)
        {
          result->status = -EIO;
        }
    }

  if (result->status == OK &&
      (result->dma_failures != 0u || result->rpmsg_failures != 0u))
    {
      result->status = -EIO;
    }

  nxmutex_unlock(&g_diag_run_mutex);
  return OK;
}

int bk7258_ap_smp_diag_witness(uint32_t index, bool failure,
                               struct bk7258_smp_diag_witness_s *witness)
{
  uint32_t per_cpu;
  uint32_t available;
  uint32_t cpu;
  uint32_t slot;
  uint32_t head;

  if (witness == NULL)
    {
      return -EINVAL;
    }

  per_cpu = failure ? DIAG_FAILURES_PER_CPU : DIAG_WITNESS_PER_CPU;
  cpu = index / per_cpu;
  slot = index % per_cpu;
  if (cpu >= 2u)
    {
      return -ENOENT;
    }

  head = failure ? g_diag_failure_count[cpu] : g_diag_witness_head[cpu];
  available = head < per_cpu ? head : per_cpu;
  if (slot >= available)
    {
      return -ENOENT;
    }

  if (failure)
    {
      memcpy(witness, &g_diag_failures[cpu][slot], sizeof(*witness));
    }
  else
    {
      /* Return the rolling ring oldest-first for this CPU. */

      memcpy(witness,
             &g_diag_witness[cpu][(head - available + slot) % per_cpu],
             sizeof(*witness));
    }

  return OK;
}

#endif /* CONFIG_BK7258_AP_SMP_DIAG */
