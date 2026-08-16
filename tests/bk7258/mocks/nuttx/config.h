/****************************************************************************
 * tests/mocks/nuttx/config.h
 *
 * Host-side build shim.  The real NuttX .config is NOT used here: we only
 * define the few CONFIG_* symbols that bk7258_rptun_mbox.c consults, and
 * deliberately leave CONFIG_SMP / CONFIG_BK7258_AP_CORE / CONFIG_BK7258_RPTUN
 * UNDEFINED so the CP-side, non-SMP code paths (and the no-op mark macro) are
 * exercised.  This keeps the unit-test surface focused on the notify state
 * machine.
 *
 * NOTE: this is a test-only mock.  The real firmware config is untouched.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_CONFIG_H
#define __MOCK_NUTTX_CONFIG_H

#include <assert.h>   /* provides static_assert() used by the implementation */

/* NuttX status conventions used throughout the implementation. */
#ifndef OK
#define OK 0
#endif
#ifndef ERROR
#define ERROR (-1)
#endif

/* The whole implementation file is guarded by this symbol. */
#define CONFIG_BK7258_RPTUN_MBOX

/* Used by bk7258_rptun_mbox_initialize() -> kthread_create(). */
#define CONFIG_BK7258_RPTUN_RX_PRIORITY 100
#define CONFIG_BK7258_RPTUN_RX_STACKSIZE 4096

#ifdef TEST_BK7258_RPTUN_CORE
#  define CONFIG_BK7258_RPTUN
#  define CONFIG_RPTUN_STATUS_TIMEOUT_MS 8
#  ifdef TEST_BK7258_RPTUN_AP
#    define CONFIG_BK7258_AP_CORE
#  endif
#endif

#endif /* __MOCK_NUTTX_CONFIG_H */
