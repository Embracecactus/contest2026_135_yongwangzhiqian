/* MCUboot logging is deliberately routed through the tiny BL2 UART hook. */
#ifndef __BK7258_BL2_MCUBOOT_LOGGING_H
#define __BK7258_BL2_MCUBOOT_LOGGING_H

#define MCUBOOT_LOG_MODULE_DECLARE(...)
#define MCUBOOT_LOG_MODULE_REGISTER(...)
#define MCUBOOT_LOG_ERR(...) do { } while (0)
#define MCUBOOT_LOG_WRN(...) do { } while (0)
#define MCUBOOT_LOG_INF(...) do { } while (0)
#define MCUBOOT_LOG_DBG(...) do { } while (0)

#endif
