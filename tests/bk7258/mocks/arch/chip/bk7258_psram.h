#ifndef __MOCK_ARCH_CHIP_BK7258_PSRAM_H
#define __MOCK_ARCH_CHIP_BK7258_PSRAM_H

#include <stdint.h>

struct bk7258_psram_info_s
{
  int32_t init_status;
  uint32_t chip_id;
  uint32_t config_value;
  uint32_t capacity;
  uintptr_t heap_base;
  uint32_t heap_size;
  uint32_t boot_test_passes;
  uint32_t boot_test_runs;
  uint32_t mpu_valid;
  uintptr_t boot_test_fail_address;
  uint32_t boot_test_expected;
  uint32_t boot_test_actual;
};

int bk7258_psram_initialize(void);
int bk7258_psram_add_system_heap(uint32_t size);
int bk7258_psram_get_info(struct bk7258_psram_info_s *info);

#endif
