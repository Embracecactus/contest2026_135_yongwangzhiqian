#ifndef __MOCK_DRIVER_FLASH_H
#define __MOCK_DRIVER_FLASH_H

#include <stdint.h>

typedef int bk_err_t;

#define BK_OK   0
#define BK_FAIL (-1)

typedef enum
{
  FLASH_PROTECT_NONE = 0,
  FLASH_UNPROTECT_LAST_BLOCK = 1
} flash_protect_type_t;

bk_err_t bk_flash_driver_init(void);
uint32_t bk_flash_get_id(void);
uint32_t bk_flash_get_current_total_size(void);
bk_err_t bk_flash_read_bytes(uint32_t address, uint8_t *buffer,
                             uint32_t size);
bk_err_t bk_flash_erase_sector(uint32_t address);
bk_err_t bk_flash_write_bytes(uint32_t address,
                              const uint8_t *buffer, uint32_t size);
bk_err_t bk_flash_set_protect_type(flash_protect_type_t type);

#endif /* __MOCK_DRIVER_FLASH_H */
