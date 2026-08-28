/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __BK7258_BL2_ASSERT_H
#define __BK7258_BL2_ASSERT_H

void bk7258_bl2_panic(void);
#define assert(expr) do { if (!(expr)) bk7258_bl2_panic(); } while (0)

#endif
