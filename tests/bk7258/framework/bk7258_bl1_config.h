/*
 * Host-only BL1 configuration fixture.
 *
 * Production BL1 receives these values from the generated build-local
 * config header.  Host tests must provide the same explicit contract instead
 * of relying on retired header defaults.  No production source includes this
 * file, and the values deliberately exercise the signed/OTP policy paths.
 */

#ifndef __TESTS_BK7258_BL1_CONFIG_H
#define __TESTS_BK7258_BL1_CONFIG_H

#define BK7258_BL1_SIGNED                     1
#define BK7258_BL1_USE_BL2                     1
#define BK7258_BL1_MANIFEST_ENFORCE            1
#define BK7258_BL1_MANIFEST_RAW_PAGE           1
#define BK7258_BL1_BOOT_CONTROL_STAGING        0
#define BK7258_BL1_OTP_ROOT_POLICY             1
#define BK7258_BL1_TRUSTENGINE_PROBE           0
#define BK7258_BL1_MANIFEST_MIN_IMAGE_VERSION  1

#define BK7258_BL1_SWD_ENABLE                  0
#define BK7258_BL1_SWD_PIN_GROUP               0
#define BK7258_BL1_SWD_TARGET                  0
#define BK7258_BL1_SWD_BOOT_HOLD               0
#define BK7258_BL1_CONSOLE_UART                1
#define BK7258_BL1_CONSOLE_BAUD                115200
#define BK7258_BL1_CONSOLE_DATA_BITS           8
#define BK7258_BL1_CONSOLE_PARITY              0
#define BK7258_BL1_CONSOLE_STOP_BITS           1
#define BK7258_BL1_UART2_PIN_GROUP             0

#endif /* __TESTS_BK7258_BL1_CONFIG_H */
