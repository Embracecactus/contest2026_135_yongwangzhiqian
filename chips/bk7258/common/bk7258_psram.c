/****************************************************************************
 * chips/bk7258/common/
 * bk7258_psram.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-owned NuttX wrapper for the official Beken PSRAM driver.  CP alone
 * initializes and owns the hardware.  CP and AP then use disjoint NuttX MM
 * heaps matching the immutable SDK bundle's generated 8 MiB layout.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/mm/mm.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/spinlock.h>

#include <arch/chip/bk7258_psram.h>

#ifndef CONFIG_BK7258_AP_CORE
#  include <components/system.h>
#endif
#include <driver/psram.h>

#ifdef CONFIG_BK7258_PSRAM_MEDIA
#  include <components/media_types.h>

/* v3.1.1.9 exports these public media-utils APIs from libmedia_utils.a,
 * while the pinned armino-as-lib header subset omits psram_mem_slab.h. */

extern void bk_psram_frame_buffer_init(void);
extern void *bk_psram_frame_buffer_malloc(psram_heap_type_t type,
                                          uint32_t size);
extern void bk_psram_frame_buffer_free(void *memory);
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_PSRAM_ID_APS6408L       0x8d09u
#define BK7258_PSRAM_ID_APS128XXO      0x8d08u
#define BK7258_PSRAM_ID_W955D8_1C      0x1c8fu
#define BK7258_PSRAM_ID_W955D8_1F      0x1f8fu

/* The official HAL rewrites register 0 after detecting an APS device:
 * APS6408L becomes 0x8d13 and APS128XXO becomes 0x8d1a.  Preserve the
 * canonical pre-configuration ID in chip_id and expose the observed value
 * separately for evidence.  The Winbond ID remains at register 0x01000000.
 */

#define BK7258_PSRAM_CONFIG_APS6408L    0x8d13u
#define BK7258_PSRAM_CONFIG_APS128XXO   0x8d1au
#define BK7258_PSRAM_W955D8_ID_ADDR     0x01000000u

#define BK7258_MPU_CTRL                (*(volatile uint32_t *)0xe000ed94u)
#define BK7258_MPU_RNR                 (*(volatile uint32_t *)0xe000ed98u)
#define BK7258_MPU_RBAR                (*(volatile uint32_t *)0xe000ed9cu)
#define BK7258_MPU_RLAR                (*(volatile uint32_t *)0xe000eda0u)
#define BK7258_MPU_MAIR0               (*(volatile uint32_t *)0xe000edc0u)
#define BK7258_MPU_PSRAM_REGION        6u
#define BK7258_MPU_PSRAM_RBAR          0x60000002u
#define BK7258_MPU_PSRAM_RLAR          0x63ffffe3u
#define BK7258_MPU_ATTR1_MASK          0x0000ff00u
#define BK7258_MPU_ATTR1_NOCACHE       0x00004400u
#define BK7258_MPU_CTRL_ENABLE         0x7u

#define BK7258_PSRAM_ALIAS_LOW         (BK7258_PSRAM_BASE + 0x1000u)
#define BK7258_PSRAM_ALIAS_HIGH        \
  (BK7258_PSRAM_BASE + BK7258_PSRAM_8M_SIZE + 0x1000u)
#define BK7258_PSRAM_ALIAS_LOW_PATTERN  0x7258a55au
#define BK7258_PSRAM_ALIAS_HIGH_PATTERN 0x58a572c3u

/* Keep the optional destructive boot-test implementation after all normal
 * .text input sections.  The imported SDK is layout-sensitive on this STAR
 * core, so enabling diagnostics must not move the SDK's executable code.
 */

#define BK7258_PSRAM_BOOT_TEXT \
  __attribute__((noinline, noclone, used, section(".psram_boot_text")))

#ifndef CONFIG_BK7258_AP_CORE
/* The immutable static-library SDK bundle exports the PM implementation and
 * pwr_clk.h, but that public header includes the private SoC sys_types.h which
 * is not shipped in the bundle.  Keep that incomplete header out of NuttX and
 * describe only the two integer ABI values used by this board wrapper.  The
 * N14 verifier checks both values and the function declaration against the
 * matching full SDK source before accepting the final ELF.
 */

#  define BK7258_SDK_PM_PSRAM_AS_MEM 10
#  define BK7258_SDK_PM_POWER_ON      0

extern bk_err_t bk_pm_module_vote_psram_ctrl(int module, int power_state);
#endif

#ifdef CONFIG_BK7258_PSRAM_BOOT_TEST
#  define BK7258_PSRAM_BOOT_TEST_GATE 0x72580001u
#else
#  define BK7258_PSRAM_BOOT_TEST_GATE 0x72580000u
#endif

#ifndef CONFIG_BK7258_PSRAM_TEST_STACKSIZE
#  define CONFIG_BK7258_PSRAM_TEST_STACKSIZE 2048
#endif

#ifdef CONFIG_BK7258_AP_CORE
#  define BK7258_PSRAM_LOCAL_HEAP_BASE BK7258_PSRAM_AP_HEAP_BASE
#  define BK7258_PSRAM_LOCAL_HEAP_SIZE BK7258_PSRAM_AP_HEAP_SIZE
#  define BK7258_PSRAM_HEAP_NAME       "bk7258-ap-psram"
#else
#  define BK7258_PSRAM_LOCAL_HEAP_BASE BK7258_PSRAM_CP_HEAP_BASE
#  define BK7258_PSRAM_LOCAL_HEAP_SIZE BK7258_PSRAM_CP_HEAP_SIZE
#  define BK7258_PSRAM_HEAP_NAME       "bk7258-cp-psram"
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_psram_test_context_s
{
  struct bk7258_psram_test_result_s *result;
  uint32_t index;
  uint32_t iterations;
  uint32_t expected_cpu;
  int status;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct mm_heap_s *g_bk7258_psram_heap;
static void *g_bk7258_psram_system_heap;
static size_t g_bk7258_psram_system_heap_size;
static spinlock_t g_bk7258_psram_lock = SP_UNLOCKED;
static struct bk7258_psram_info_s g_bk7258_psram_info;
#ifdef CONFIG_BK7258_PSRAM_MEDIA
static mutex_t g_bk7258_psram_media_lock = NXMUTEX_INITIALIZER;
static bool g_bk7258_psram_media_initialized;
#endif
#ifndef CONFIG_BK7258_AP_CORE
static bool g_bk7258_psram_hardware_attempted;
static bool g_bk7258_psram_hardware_ready;
static volatile uint32_t g_bk7258_psram_boot_test_gate =
  BK7258_PSRAM_BOOT_TEST_GATE;
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

#ifndef CONFIG_BK7258_AP_CORE
/* This HAL read helper is exported by the official libcommon.a but is not a
 * public driver API.  It is used once, with no concurrent PSRAM users, to
 * report the device ID that bk_psram_init() has already configured.
 */

extern uint32_t psram_hal_cmd_read(uint32_t addr);
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifndef CONFIG_BK7258_AP_CORE
static inline void bk7258_psram_barrier(void)
{
  __asm volatile ("dsb sy" ::: "memory");
}

static void bk7258_psram_mpu_initialize(void)
{
  uint32_t control = BK7258_MPU_CTRL;
  uint32_t mair0;
  uint32_t rnr = BK7258_MPU_RNR;

  BK7258_MPU_CTRL = 0;
  __asm volatile ("dsb sy; isb sy" ::: "memory");

  mair0 = BK7258_MPU_MAIR0;
  mair0 &= ~BK7258_MPU_ATTR1_MASK;
  mair0 |= BK7258_MPU_ATTR1_NOCACHE;
  BK7258_MPU_MAIR0 = mair0;

  BK7258_MPU_RNR = BK7258_MPU_PSRAM_REGION;
  BK7258_MPU_RBAR = BK7258_MPU_PSRAM_RBAR;
  BK7258_MPU_RLAR = BK7258_MPU_PSRAM_RLAR;
  BK7258_MPU_RNR = rnr;

  BK7258_MPU_CTRL = control | BK7258_MPU_CTRL_ENABLE;
  __asm volatile ("dsb sy; isb sy" ::: "memory");
}

static void bk7258_psram_record_failure(uint32_t address,
                                         uint32_t expected,
                                         uint32_t actual)
{
  if (g_bk7258_psram_info.boot_test_fail_address == 0)
    {
      g_bk7258_psram_info.boot_test_fail_address = address;
      g_bk7258_psram_info.boot_test_expected = expected;
      g_bk7258_psram_info.boot_test_actual = actual;
    }
}

static int bk7258_psram_verify_16m_not_aliased(void)
{
  volatile uint32_t *low =
    (volatile uint32_t *)(uintptr_t)BK7258_PSRAM_ALIAS_LOW;
  volatile uint32_t *high =
    (volatile uint32_t *)(uintptr_t)BK7258_PSRAM_ALIAS_HIGH;
  uint32_t saved_low = *low;
  uint32_t saved_high = *high;
  uint32_t observed_low;
  uint32_t observed_high;

  *low = BK7258_PSRAM_ALIAS_LOW_PATTERN;
  bk7258_psram_barrier();
  *high = BK7258_PSRAM_ALIAS_HIGH_PATTERN;
  bk7258_psram_barrier();

  observed_low = *low;
  observed_high = *high;

  *high = saved_high;
  bk7258_psram_barrier();
  *low = saved_low;
  bk7258_psram_barrier();

  if (observed_low != BK7258_PSRAM_ALIAS_LOW_PATTERN ||
      observed_high != BK7258_PSRAM_ALIAS_HIGH_PATTERN)
    {
      bk7258_psram_record_failure(BK7258_PSRAM_ALIAS_HIGH,
                                  BK7258_PSRAM_ALIAS_HIGH_PATTERN,
                                  observed_high);
      return -EIO;
    }

  return OK;
}

static int BK7258_PSRAM_BOOT_TEXT
bk7258_psram_boot_test_word(volatile uint32_t *word, uint32_t expected)
{
  uint32_t actual = *word;

  if (actual != expected)
    {
      bk7258_psram_record_failure((uint32_t)(uintptr_t)word,
                                  expected, actual);
      return -EIO;
    }

  return OK;
}

static int BK7258_PSRAM_BOOT_TEXT
bk7258_psram_boot_test(uint32_t capacity)
{
  volatile uint32_t *base =
    (volatile uint32_t *)(uintptr_t)BK7258_PSRAM_BASE;
  uint32_t words = capacity / sizeof(uint32_t);
  uint32_t offset;
  uint32_t pattern;
  uint32_t bit;

  g_bk7258_psram_info.boot_test_runs++;

  /* Data-bus walking-one gate. */

  for (bit = 1u; bit != 0; bit <<= 1)
    {
      base[0] = bit;
      bk7258_psram_barrier();
      if (bk7258_psram_boot_test_word(&base[0], bit) < 0)
        {
          return -EIO;
        }
    }

  /* Address-bus aliases at every power-of-two word offset. */

  base[0] = 0xaaaaaaaau;
  for (offset = 1u; offset < words; offset <<= 1)
    {
      base[offset] = 0x55555555u;
    }

  bk7258_psram_barrier();
  if (bk7258_psram_boot_test_word(&base[0], 0xaaaaaaaau) < 0)
    {
      return -EIO;
    }

  for (offset = 1u; offset < words; offset <<= 1)
    {
      if (bk7258_psram_boot_test_word(&base[offset], 0x55555555u) < 0)
        {
          return -EIO;
        }
    }

  /* Full-capacity address-derived pattern and inverse. */

  for (offset = 0; offset < words; offset++)
    {
      base[offset] = 0xa5a50000u ^ offset;
    }

  bk7258_psram_barrier();
  for (offset = 0; offset < words; offset++)
    {
      pattern = 0xa5a50000u ^ offset;
      if (bk7258_psram_boot_test_word(&base[offset], pattern) < 0)
        {
          return -EIO;
        }

      base[offset] = ~pattern;
    }

  bk7258_psram_barrier();
  for (offset = 0; offset < words; offset++)
    {
      pattern = ~(0xa5a50000u ^ offset);
      if (bk7258_psram_boot_test_word(&base[offset], pattern) < 0)
        {
          return -EIO;
        }

      base[offset] = 0;
    }

  bk7258_psram_barrier();
  for (offset = 0; offset < words; offset++)
    {
      if (bk7258_psram_boot_test_word(&base[offset], 0) < 0)
        {
          return -EIO;
        }
    }

  g_bk7258_psram_info.boot_test_passes++;
  return OK;
}

static int BK7258_PSRAM_BOOT_TEXT
bk7258_psram_run_boot_test(uint32_t capacity)
{
  if ((g_bk7258_psram_boot_test_gate & 1u) == 0)
    {
      return OK;
    }

  return bk7258_psram_boot_test(capacity);
}
#endif /* !CONFIG_BK7258_AP_CORE */

static int bk7258_psram_heap_initialize(void)
{
  struct mm_heap_config_s config;

  /* The BK7258 PSRAM bus does not complete Arm exclusive stores.  Keep the
   * mm_heap_s control block (and its atomic mutex) in internal SRAM while
   * using PSRAM only for allocation nodes and payloads.  mm_initialize()
   * would place the control block at the beginning of PSRAM and spin forever
   * in the first STLEX performed by mm_addregion().
   */

  memset(&config, 0, sizeof(config));
  config.name = BK7258_PSRAM_HEAP_NAME;
  config.start = (void *)(uintptr_t)BK7258_PSRAM_LOCAL_HEAP_BASE;
  config.size = BK7258_PSRAM_LOCAL_HEAP_SIZE;
  config.allocheap = true;

  g_bk7258_psram_heap = NULL;
  mm_initialize_heap(&config, &g_bk7258_psram_heap);
  if (g_bk7258_psram_heap == NULL)
    {
      return -ENOMEM;
    }

  if (bk_psram_heap_init_flag_set(true) != BK_OK)
    {
      return -EIO;
    }

  g_bk7258_psram_info.heap_base = BK7258_PSRAM_LOCAL_HEAP_BASE;
  g_bk7258_psram_info.heap_size = BK7258_PSRAM_LOCAL_HEAP_SIZE;
  g_bk7258_psram_info.ready = 1;
  return OK;
}

static size_t bk7258_psram_allocation_size(void *ptr)
{
  irqstate_t flags;
  size_t size;

  flags = spin_lock_irqsave(&g_bk7258_psram_lock);
  size = mm_malloc_size(g_bk7258_psram_heap, ptr);
  spin_unlock_irqrestore(&g_bk7258_psram_lock, flags);
  return size;
}

static void bk7258_psram_test_progress(
  struct bk7258_psram_test_result_s *result, uint32_t index,
  uint32_t iteration, uint32_t stage)
{
  volatile uint32_t *active = &result->active_iteration[index];
  volatile uint32_t *current = &result->stage[index];

  *active = iteration;
  *current = stage;
  __asm volatile ("dmb sy" ::: "memory");
}

static void *bk7258_psram_test_worker(void *arg)
{
  struct bk7258_psram_test_context_s *context = arg;
  struct bk7258_psram_test_result_s *result = context->result;
  uint32_t index = context->index;
  uint32_t iteration;

  bk7258_psram_test_progress(result, index, 0,
                             BK7258_PSRAM_TEST_STAGE_CPU_CHECK);
  result->observed_cpu[index] = (uint32_t)sched_getcpu();
  if (!bk7258_psram_mpu_valid() ||
      result->observed_cpu[index] != context->expected_cpu)
    {
      result->errors[index]++;
      context->status = -EIO;
      bk7258_psram_test_progress(result, index, 0,
                                 BK7258_PSRAM_TEST_STAGE_ERROR);
      return NULL;
    }

  for (iteration = 0; iteration < context->iterations; iteration++)
    {
      uint32_t words = 16u +
        ((iteration * 37u + index * 17u) & 0xffu);
      uint32_t expanded_words = words + 32u;
      uint32_t pattern = 0x72580000u ^ (index << 12) ^ iteration;
      uint32_t *memory;
      uint32_t word;

      bk7258_psram_test_progress(result, index, iteration,
                                 BK7258_PSRAM_TEST_STAGE_ALLOC_ENTER);
      if ((iteration & 0x0fu) == 0)
        {
          memory = bk7258_psram_zalloc(words * sizeof(uint32_t));
          if (memory == NULL)
            {
              result->errors[index]++;
              context->status = -ENOMEM;
              bk7258_psram_test_progress(
                result, index, iteration, BK7258_PSRAM_TEST_STAGE_ERROR);
              break;
            }

          bk7258_psram_test_progress(
            result, index, iteration, BK7258_PSRAM_TEST_STAGE_ZERO_VERIFY);
          for (word = 0; word < words; word++)
            {
              if (memory[word] != 0)
                {
                  result->errors[index]++;
                  context->status = -EIO;
                  break;
                }
            }
        }
      else
        {
          memory = bk7258_psram_malloc(words * sizeof(uint32_t));
          if (memory == NULL)
            {
              result->errors[index]++;
              context->status = -ENOMEM;
              bk7258_psram_test_progress(
                result, index, iteration, BK7258_PSRAM_TEST_STAGE_ERROR);
              break;
            }
        }

      bk7258_psram_test_progress(result, index, iteration,
                                 BK7258_PSRAM_TEST_STAGE_ALLOC_RETURN);
      bk7258_psram_test_progress(result, index, iteration,
                                 BK7258_PSRAM_TEST_STAGE_WRITE);
      for (word = 0; word < words; word++)
        {
          memory[word] = pattern ^ word;
        }

      if ((iteration & 3u) == 3u)
        {
          bk7258_psram_test_progress(
            result, index, iteration, BK7258_PSRAM_TEST_STAGE_REALLOC_ENTER);
          uint32_t *expanded = bk7258_psram_realloc(
            memory, expanded_words * sizeof(uint32_t));
          if (expanded == NULL)
            {
              bk7258_psram_free(memory);
              result->errors[index]++;
              context->status = -ENOMEM;
              bk7258_psram_test_progress(
                result, index, iteration, BK7258_PSRAM_TEST_STAGE_ERROR);
              break;
            }

          memory = expanded;
          bk7258_psram_test_progress(
            result, index, iteration, BK7258_PSRAM_TEST_STAGE_REALLOC_RETURN);
        }

      bk7258_psram_test_progress(result, index, iteration,
                                 BK7258_PSRAM_TEST_STAGE_DATA_VERIFY);
      for (word = 0; word < words; word++)
        {
          if (memory[word] != (pattern ^ word))
            {
              result->errors[index]++;
              context->status = -EIO;
              break;
            }
        }

      bk7258_psram_test_progress(result, index, iteration,
                                 BK7258_PSRAM_TEST_STAGE_FREE_ENTER);
      bk7258_psram_free(memory);
      bk7258_psram_test_progress(result, index, iteration,
                                 BK7258_PSRAM_TEST_STAGE_FREE_RETURN);
      if (context->status < 0)
        {
          bk7258_psram_test_progress(
            result, index, iteration, BK7258_PSRAM_TEST_STAGE_ERROR);
          break;
        }

      result->completed[index]++;
      bk7258_psram_test_progress(
        result, index, iteration, BK7258_PSRAM_TEST_STAGE_ITERATION_DONE);
    }

  if (context->status == OK && result->completed[index] == context->iterations)
    {
      bk7258_psram_test_progress(result, index, context->iterations,
                                 BK7258_PSRAM_TEST_STAGE_COMPLETE);
    }

  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifndef CONFIG_BK7258_AP_CORE
int bk7258_psram_early_initialize(void)
{
  int ret;

  if (g_bk7258_psram_hardware_ready)
    {
      return OK;
    }

  if (g_bk7258_psram_hardware_attempted)
    {
      return g_bk7258_psram_info.init_status == -EINPROGRESS ?
             -EBUSY : g_bk7258_psram_info.init_status;
    }

  g_bk7258_psram_hardware_attempted = true;
  memset(&g_bk7258_psram_info, 0, sizeof(g_bk7258_psram_info));
  g_bk7258_psram_info.init_status = -EINPROGRESS;

  bk7258_psram_mpu_initialize();
  if (!bk7258_psram_mpu_valid())
    {
      ret = -EACCES;
      goto failed;
    }

  /* Follow the official SDK's normal PSRAM ownership path.  The PM vote
   * records the AS_MEM consumer, raises/retains the required VDDDIG level,
   * and invokes bk_psram_init() with the SDK's bounded retry sequence.  A
   * direct bk_psram_init() call can make the memory initially readable while
   * leaving the PM state at "unused", which is not a valid runtime handoff
   * for the AP consumers.
   *
   * The static SDK logger is routed to NuttX syslog by the board wrapper;
   * suppress its low-level initialization chatter and let
   * board_app_initialize() emit the authoritative BPSR result.
   */

  bk_set_printf_enable(0);
  ret = bk_pm_module_vote_psram_ctrl(
    BK7258_SDK_PM_PSRAM_AS_MEM, BK7258_SDK_PM_POWER_ON);
  bk_set_printf_enable(1);
  if (ret != BK_OK)
    {
      ret = -EIO;
      goto failed;
    }

  g_bk7258_psram_info.config_value = psram_hal_cmd_read(0) & 0xffffu;
  switch (g_bk7258_psram_info.config_value)
    {
      case BK7258_PSRAM_CONFIG_APS128XXO:
      case BK7258_PSRAM_ID_APS128XXO:
        g_bk7258_psram_info.chip_id = BK7258_PSRAM_ID_APS128XXO;
        g_bk7258_psram_info.capacity = BK7258_PSRAM_16M_SIZE;
        ret = bk7258_psram_verify_16m_not_aliased();
        if (ret < 0)
          {
            goto failed;
          }
        break;

      case BK7258_PSRAM_CONFIG_APS6408L:
      case BK7258_PSRAM_ID_APS6408L:
        g_bk7258_psram_info.chip_id = BK7258_PSRAM_ID_APS6408L;
        g_bk7258_psram_info.capacity = BK7258_PSRAM_8M_SIZE;
        break;

      default:
        g_bk7258_psram_info.config_value =
          psram_hal_cmd_read(BK7258_PSRAM_W955D8_ID_ADDR) & 0xffffu;
        if (g_bk7258_psram_info.config_value ==
              BK7258_PSRAM_ID_W955D8_1C ||
            g_bk7258_psram_info.config_value ==
              BK7258_PSRAM_ID_W955D8_1F)
          {
            g_bk7258_psram_info.chip_id =
              g_bk7258_psram_info.config_value;
            ret = -ENOSPC;
          }
        else
          {
            ret = -ENODEV;
          }

        goto failed;
    }

  ret = bk7258_psram_run_boot_test(g_bk7258_psram_info.capacity);
  if (ret < 0)
    {
      goto failed;
    }

  g_bk7258_psram_info.mpu_valid = 1;
  g_bk7258_psram_hardware_ready = true;
  return OK;

failed:
  g_bk7258_psram_info.init_status = ret;
  g_bk7258_psram_hardware_ready = false;
  return ret;
}
#endif /* !CONFIG_BK7258_AP_CORE */

int bk7258_psram_initialize(void)
{
  int ret;

  if (g_bk7258_psram_info.ready != 0)
    {
      return OK;
    }

#ifdef CONFIG_BK7258_AP_CORE
  /* CP has already initialized and left PSRAM powered before releasing AP.
   * AP only validates its per-core MPU view and creates its disjoint heap.
   */

  memset(&g_bk7258_psram_info, 0, sizeof(g_bk7258_psram_info));
  g_bk7258_psram_info.init_status = -EINPROGRESS;
  g_bk7258_psram_info.capacity = BK7258_PSRAM_8M_SIZE;
#else
  /* Normally completed by CP board_app_initialize() after the immutable
   * PHY/RF calibration path.  Keep this fallback in the shared role wrapper
   * so alternate board bring-up paths still fail closed.
   */

  ret = bk7258_psram_early_initialize();
  if (ret < 0)
    {
      goto failed;
    }

  g_bk7258_psram_info.init_status = -EINPROGRESS;
#endif /* CONFIG_BK7258_AP_CORE */

  ret = bk7258_psram_heap_initialize();
  if (ret < 0)
    {
      goto failed;
    }

  g_bk7258_psram_info.mpu_valid = bk7258_psram_mpu_valid() ? 1u : 0u;
  if (g_bk7258_psram_info.mpu_valid == 0)
    {
      ret = -EACCES;
      goto failed;
    }

  g_bk7258_psram_info.init_status = OK;
  return OK;

failed:
  g_bk7258_psram_info.init_status = ret;
  g_bk7258_psram_info.ready = 0;
  return ret;
}

bool bk7258_psram_ready(void)
{
  return g_bk7258_psram_info.ready != 0 &&
         g_bk7258_psram_heap != NULL;
}

bool bk7258_psram_address(const void *ptr)
{
  uintptr_t address = (uintptr_t)ptr;

  return address >= BK7258_PSRAM_BASE &&
         address < BK7258_PSRAM_BASE + BK7258_PSRAM_16M_SIZE;
}

bool bk7258_psram_heap_contains(const void *ptr)
{
  uintptr_t address = (uintptr_t)ptr;

  return address >= BK7258_PSRAM_LOCAL_HEAP_BASE &&
         address < BK7258_PSRAM_LOCAL_HEAP_BASE +
                   BK7258_PSRAM_LOCAL_HEAP_SIZE;
}

bool bk7258_psram_system_heap_contains(const void *ptr)
{
  uintptr_t address = (uintptr_t)ptr;
  uintptr_t start = (uintptr_t)g_bk7258_psram_system_heap;

  return g_bk7258_psram_system_heap != NULL &&
         address > start &&
         address < start + g_bk7258_psram_system_heap_size;
}

bool bk7258_psram_mpu_valid(void)
{
  uint32_t rnr = BK7258_MPU_RNR;
  uint32_t rbar;
  uint32_t rlar;

  BK7258_MPU_RNR = BK7258_MPU_PSRAM_REGION;
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  rbar = BK7258_MPU_RBAR;
  rlar = BK7258_MPU_RLAR;
  BK7258_MPU_RNR = rnr;
  __asm volatile ("dsb sy; isb sy" ::: "memory");

  return (BK7258_MPU_CTRL & BK7258_MPU_CTRL_ENABLE) ==
           BK7258_MPU_CTRL_ENABLE &&
         (BK7258_MPU_MAIR0 & BK7258_MPU_ATTR1_MASK) ==
           BK7258_MPU_ATTR1_NOCACHE &&
         rbar == BK7258_MPU_PSRAM_RBAR &&
         rlar == BK7258_MPU_PSRAM_RLAR;
}

void *bk7258_psram_malloc(size_t size)
{
  irqstate_t flags;
  void *memory;

  if (!bk7258_psram_ready())
    {
      return NULL;
    }

  /* NuttX's private heap uses a sleeping recursive mutex.  The official
   * BK7258 AP SMP heap instead serializes allocator metadata under an
   * internal-SRAM spinlock/critical section.  Add the same board-level gate
   * so the NuttX heap mutex is never contended between the two AP CPUs.  The
   * heap control block and this lock both remain in internal SRAM because
   * the PSRAM bus cannot complete Arm exclusive stores.
   */

  flags = spin_lock_irqsave(&g_bk7258_psram_lock);
  memory = mm_malloc(g_bk7258_psram_heap, size);
  spin_unlock_irqrestore(&g_bk7258_psram_lock, flags);
  return memory;
}

void *bk7258_psram_zalloc(size_t size)
{
  void *memory;

  if (!bk7258_psram_ready())
    {
      return NULL;
    }

  memory = bk7258_psram_malloc(size);
  if (memory != NULL)
    {
      memset(memory, 0, size);
    }

  return memory;
}

void *bk7258_psram_realloc(void *ptr, size_t size)
{
  void *replacement;
  size_t copy_size;

  if (!bk7258_psram_ready() ||
      (ptr != NULL && !bk7258_psram_heap_contains(ptr)))
    {
      return NULL;
    }

  if (ptr == NULL)
    {
      return bk7258_psram_malloc(size);
    }

  /* Match the official BK7258 AP psram_realloc() wrapper: allocate a new
   * PSRAM block, copy the retained payload, then free the old block.  Do not
   * use mm_realloc() here.  Its in-place neighbour manipulation is valid for
   * ordinary RAM, but repeated AP SMP restarts have shown a core stuck in
   * that path while the other core completes its PSRAM workload.
   *
   * The official wrapper copies the requested new size unconditionally.
   * Bound the copy by NuttX's actual old allocation size so shrinking and
   * growth cannot read beyond the old block.  The caller still owns ptr, so
   * its allocation header remains stable between this query and mm_free().
   */

  copy_size = bk7258_psram_allocation_size(ptr);
  replacement = bk7258_psram_malloc(size);
  if (replacement == NULL)
    {
      return NULL;
    }

  if (copy_size > size)
    {
      copy_size = size;
    }

  memcpy(replacement, ptr, copy_size);
  bk7258_psram_free(ptr);
  return replacement;
}

void bk7258_psram_free(void *ptr)
{
  irqstate_t flags;

  if (ptr == NULL)
    {
      return;
    }

  if (bk7258_psram_ready() && bk7258_psram_heap_contains(ptr))
    {
      flags = spin_lock_irqsave(&g_bk7258_psram_lock);
      mm_free(g_bk7258_psram_heap, ptr);
      spin_unlock_irqrestore(&g_bk7258_psram_lock, flags);
    }
}

int bk7258_psram_add_system_heap(size_t size)
{
  void *memory;

  if (!bk7258_psram_ready() || size == 0 ||
      size >= BK7258_PSRAM_LOCAL_HEAP_SIZE)
    {
      return -EINVAL;
    }

  if (g_bk7258_psram_system_heap != NULL)
    {
      return g_bk7258_psram_system_heap_size == size ? OK : -EALREADY;
    }

  memory = bk7258_psram_malloc(size);
  if (memory == NULL)
    {
      return -ENOMEM;
    }

  /* The private PSRAM heap keeps the enclosing block allocated permanently.
   * NuttX may then safely manage allocations inside it as a second region;
   * the two allocators never see overlapping free space.  The NuttX heap
   * control block and lock remain in exclusive-store-capable internal SRAM.
   */

  kumm_addregion(memory, size);
  g_bk7258_psram_system_heap = memory;
  g_bk7258_psram_system_heap_size = size;
  return OK;
}

#ifdef CONFIG_BK7258_PSRAM_MEDIA
int bk7258_psram_media_initialize(void)
{
  int ret;

  if (!bk7258_psram_ready())
    {
      return -ENODEV;
    }

  ret = nxmutex_lock(&g_bk7258_psram_media_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!g_bk7258_psram_media_initialized)
    {
      bk_psram_frame_buffer_init();
      g_bk7258_psram_media_initialized = true;
    }

  nxmutex_unlock(&g_bk7258_psram_media_lock);
  return OK;
}

void *bk7258_psram_media_malloc(enum bk7258_psram_media_heap_e heap,
                                size_t size)
{
  void *memory = NULL;

  if (size == 0 || size > UINT32_MAX ||
      heap < BK7258_PSRAM_MEDIA_USER ||
      heap > BK7258_PSRAM_MEDIA_YUV ||
      bk7258_psram_media_initialize() < 0)
    {
      return NULL;
    }

  if (nxmutex_lock(&g_bk7258_psram_media_lock) < 0)
    {
      return NULL;
    }

  memory = bk_psram_frame_buffer_malloc((psram_heap_type_t)heap,
                                        (uint32_t)size);
  nxmutex_unlock(&g_bk7258_psram_media_lock);
  return memory;
}

void bk7258_psram_media_free(void *ptr)
{
  uintptr_t address = (uintptr_t)ptr;

  if (ptr == NULL || !g_bk7258_psram_media_initialized ||
      address < BK7258_PSRAM_MEDIA_BASE ||
      address >= BK7258_PSRAM_MEDIA_BASE + BK7258_PSRAM_MEDIA_SIZE ||
      nxmutex_lock(&g_bk7258_psram_media_lock) < 0)
    {
      return;
    }

  bk_psram_frame_buffer_free(ptr);
  nxmutex_unlock(&g_bk7258_psram_media_lock);
}
#endif

size_t bk7258_psram_total_size(void)
{
  if (!bk7258_psram_ready())
    {
      return 0;
    }

  /* mm_mallinfo() includes sizeof(mm_heap_s) in uordblks even though this
   * heap's control block was allocated from internal SRAM.  Report the
   * configured PSRAM region instead of overstating its usable capacity.
   */

  return g_bk7258_psram_info.heap_size;
}

size_t bk7258_psram_free_size(void)
{
  struct mallinfo info;
  irqstate_t flags;

  if (!bk7258_psram_ready())
    {
      return 0;
    }

  flags = spin_lock_irqsave(&g_bk7258_psram_lock);
  info = mm_mallinfo(g_bk7258_psram_heap);
  spin_unlock_irqrestore(&g_bk7258_psram_lock, flags);
  return info.fordblks;
}

size_t bk7258_psram_minimum_free_size(void)
{
  struct mallinfo info;
  irqstate_t flags;
  size_t total;

  if (!bk7258_psram_ready())
    {
      return 0;
    }

  flags = spin_lock_irqsave(&g_bk7258_psram_lock);
  info = mm_mallinfo(g_bk7258_psram_heap);
  spin_unlock_irqrestore(&g_bk7258_psram_lock, flags);
  total = info.fordblks + info.uordblks;
  return total >= info.usmblks ? total - info.usmblks : 0;
}

size_t bk7258_psram_used_size(void)
{
  size_t free_size;

  if (!bk7258_psram_ready())
    {
      return 0;
    }

  free_size = bk7258_psram_free_size();
  return g_bk7258_psram_info.heap_size >= free_size ?
         g_bk7258_psram_info.heap_size - free_size : 0;
}

int bk7258_psram_get_info(struct bk7258_psram_info_s *info)
{
  if (info == NULL)
    {
      return -EINVAL;
    }

  *info = g_bk7258_psram_info;
  info->heap_total = (uint32_t)bk7258_psram_total_size();
  info->heap_free = (uint32_t)bk7258_psram_free_size();
  info->heap_minimum_free =
    (uint32_t)bk7258_psram_minimum_free_size();
  info->mpu_valid = bk7258_psram_mpu_valid() ? 1u : 0u;
  info->ready = bk7258_psram_ready() ? 1u : 0u;
  return OK;
}

int bk7258_psram_heap_test(uint32_t iterations, bool concurrent,
                           struct bk7258_psram_test_result_s *result)
{
  struct bk7258_psram_test_context_s contexts[2];
  int ret = OK;

  if (result == NULL || iterations == 0 || !bk7258_psram_ready())
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  memset(contexts, 0, sizeof(contexts));
  result->requested_iterations = iterations;
  result->free_before = (uint32_t)bk7258_psram_free_size();

  contexts[0].result = result;
  contexts[0].index = 0;
  contexts[0].iterations = iterations;
  contexts[0].expected_cpu = (uint32_t)sched_getcpu();

#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SMP)
  if (concurrent)
    {
      pthread_attr_t attrs[2];
      pthread_t threads[2];
      bool created[2] = {false, false};
      uint32_t index;

      for (index = 0; index < 2; index++)
        {
          cpu_set_t cpuset = (cpu_set_t)1u << index;
          bool attr_initialized = false;
          int pthread_ret;

          contexts[index].result = result;
          contexts[index].index = index;
          contexts[index].iterations = iterations;
          contexts[index].expected_cpu = index;

          pthread_ret = pthread_attr_init(&attrs[index]);
          if (pthread_ret == 0)
            {
              attr_initialized = true;
              pthread_ret = pthread_attr_setstacksize(
                &attrs[index], CONFIG_BK7258_PSRAM_TEST_STACKSIZE);
            }

          if (pthread_ret == 0)
            {
              pthread_ret = pthread_attr_setaffinity_np(
                &attrs[index], sizeof(cpuset), &cpuset);
            }

          if (pthread_ret == 0)
            {
              pthread_ret = pthread_create(
                &threads[index], &attrs[index], bk7258_psram_test_worker,
                &contexts[index]);
            }

          if (attr_initialized)
            {
              (void)pthread_attr_destroy(&attrs[index]);
            }
          if (pthread_ret != 0)
            {
              ret = -pthread_ret;
              break;
            }

          created[index] = true;
        }

      for (index = 0; index < 2; index++)
        {
          if (created[index])
            {
              int pthread_ret = pthread_join(threads[index], NULL);
              if (pthread_ret != 0 && ret == OK)
                {
                  ret = -pthread_ret;
                }
            }
        }

      if (ret == OK &&
          (contexts[0].status < 0 || contexts[1].status < 0))
        {
          ret = contexts[0].status < 0 ? contexts[0].status :
                                        contexts[1].status;
        }
    }
  else
#else
  (void)concurrent;
#endif
    {
      (void)bk7258_psram_test_worker(&contexts[0]);
      ret = contexts[0].status;
    }

  result->free_after = (uint32_t)bk7258_psram_free_size();
  if (ret == OK && result->free_before != result->free_after)
    {
      ret = -EIO;
    }

  result->status = ret;
  return ret;
}
