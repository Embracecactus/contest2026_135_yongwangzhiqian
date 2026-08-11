/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/
 * bk7258_psram.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_INCLUDE_BK7258_BK7258_PSRAM_H
#define __ARCH_ARM_INCLUDE_BK7258_BK7258_PSRAM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_PSRAM_BASE               0x60000000u
#define BK7258_PSRAM_8M_SIZE            0x00800000u
#define BK7258_PSRAM_16M_SIZE           0x01000000u

/* Keep the allocator ABI identical to the v3.1.1.9 projects/app 8 MiB
 * ram_regions.csv.  The first 7 MiB remains reserved for the SDK media slabs
 * and the final 256 KiB remains reserved for AP_PSRAM_SECTION.
 */

#define BK7258_PSRAM_CP_HEAP_BASE       0x60700000u
#define BK7258_PSRAM_CP_HEAP_SIZE       0x00020000u
#define BK7258_PSRAM_AP_HEAP_BASE       0x60720000u
#define BK7258_PSRAM_AP_HEAP_SIZE       0x000a0000u
#define BK7258_PSRAM_AP_SECTION_BASE    0x607c0000u
#define BK7258_PSRAM_AP_SECTION_SIZE    0x00040000u

/* Official v3.1.1.9 8 MiB media-slab layout. */

#define BK7258_PSRAM_MEDIA_BASE         0x60000000u
#define BK7258_PSRAM_MEDIA_SIZE         0x00700000u

enum bk7258_psram_media_heap_e
{
  BK7258_PSRAM_MEDIA_USER = 0,
  BK7258_PSRAM_MEDIA_AUDIO,
  BK7258_PSRAM_MEDIA_ENCODE,
  BK7258_PSRAM_MEDIA_YUV
};

#define BK7258_PSRAM_AP_RESERVED_RESULT 0u
#define BK7258_PSRAM_AP_RESERVED_MAGIC  1u
#define BK7258_PSRAM_AP_RESERVED_HEAP   2u
#define BK7258_PSRAM_AP_RESERVED_GATE   3u
#define BK7258_PSRAM_AP_RESULT_READY    0x50535252u /* "PSRR" */
#define BK7258_PSRAM_AP_HEAP_READY      0x50535248u /* "PSRH" */
#define BK7258_PSRAM_AP_TEST_PASSED     0x50535254u /* "PSRT" */

/* Live AP SMP heap-test stages.  These values are part of the board-owned
 * CP/AP diagnostic ABI so CP can distinguish allocator lockups from data
 * corruption even when AP has not returned from pthread_join().
 */

#define BK7258_PSRAM_TEST_STAGE_NONE          0u
#define BK7258_PSRAM_TEST_STAGE_CPU_CHECK     1u
#define BK7258_PSRAM_TEST_STAGE_ALLOC_ENTER   2u
#define BK7258_PSRAM_TEST_STAGE_ALLOC_RETURN  3u
#define BK7258_PSRAM_TEST_STAGE_ZERO_VERIFY   4u
#define BK7258_PSRAM_TEST_STAGE_WRITE         5u
#define BK7258_PSRAM_TEST_STAGE_REALLOC_ENTER 6u
#define BK7258_PSRAM_TEST_STAGE_REALLOC_RETURN 7u
#define BK7258_PSRAM_TEST_STAGE_DATA_VERIFY   8u
#define BK7258_PSRAM_TEST_STAGE_FREE_ENTER    9u
#define BK7258_PSRAM_TEST_STAGE_FREE_RETURN   10u
#define BK7258_PSRAM_TEST_STAGE_ITERATION_DONE 11u
#define BK7258_PSRAM_TEST_STAGE_COMPLETE      12u
#define BK7258_PSRAM_TEST_STAGE_ERROR         13u

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bk7258_psram_info_s
{
  int32_t init_status;
  uint32_t chip_id;
  uint32_t config_value;
  uint32_t capacity;
  uint32_t heap_base;
  uint32_t heap_size;
  uint32_t heap_total;
  uint32_t heap_free;
  uint32_t heap_minimum_free;
  uint32_t boot_test_runs;
  uint32_t boot_test_passes;
  uint32_t boot_test_fail_address;
  uint32_t boot_test_expected;
  uint32_t boot_test_actual;
  uint32_t mpu_valid;
  uint32_t ready;
};

struct bk7258_psram_test_result_s
{
  int32_t status;
  uint32_t requested_iterations;
  uint32_t completed[2];
  uint32_t active_iteration[2];
  uint32_t stage[2];
  uint32_t errors[2];
  uint32_t observed_cpu[2];
  uint32_t free_before;
  uint32_t free_after;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef CONFIG_BK7258_AP_CORE
int bk7258_psram_early_initialize(void);
#endif
int bk7258_psram_initialize(void);
bool bk7258_psram_ready(void);
bool bk7258_psram_address(const void *ptr);
bool bk7258_psram_heap_contains(const void *ptr);
bool bk7258_psram_mpu_valid(void);

void *bk7258_psram_malloc(size_t size);
void *bk7258_psram_zalloc(size_t size);
void *bk7258_psram_realloc(void *ptr, size_t size);
void bk7258_psram_free(void *ptr);

#ifdef CONFIG_BK7258_PSRAM_MEDIA
int bk7258_psram_media_initialize(void);
void *bk7258_psram_media_malloc(enum bk7258_psram_media_heap_e heap,
                                size_t size);
void bk7258_psram_media_free(void *ptr);
#endif

size_t bk7258_psram_total_size(void);
size_t bk7258_psram_free_size(void);
size_t bk7258_psram_minimum_free_size(void);
size_t bk7258_psram_used_size(void);

int bk7258_psram_get_info(struct bk7258_psram_info_s *info);
int bk7258_psram_heap_test(uint32_t iterations, bool concurrent,
                           struct bk7258_psram_test_result_s *result);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_INCLUDE_BK7258_BK7258_PSRAM_H */
