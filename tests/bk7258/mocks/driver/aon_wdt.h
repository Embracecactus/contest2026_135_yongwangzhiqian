#ifndef __TEST_BK7258_DRIVER_AON_WDT_H
#define __TEST_BK7258_DRIVER_AON_WDT_H

#include <stdint.h>

typedef int bk_err_t;

#define BK_OK 0

bk_err_t bk_aon_wdt_set_period(uint32_t period);

#endif /* __TEST_BK7258_DRIVER_AON_WDT_H */
