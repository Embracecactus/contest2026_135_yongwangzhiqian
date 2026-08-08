#ifndef __BK7258_BL2_MCUBOOT_ASSERT_H
#define __BK7258_BL2_MCUBOOT_ASSERT_H

void bk7258_bl2_panic(void);
#define ASSERT(expr) do { if (!(expr)) bk7258_bl2_panic(); } while (0)

#endif
