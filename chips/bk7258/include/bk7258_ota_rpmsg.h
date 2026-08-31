/****************************************************************************
 * chips/bk7258/include/bk7258_ota_rpmsg.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_RPMSG_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_RPMSG_H

#include <nuttx/config.h>

#include <stdint.h>

#include <arch/chip/bk7258_ota.h>
#include <arch/chip/bk7258_ota_manager.h>
#include <arch/chip/bk7258_usbmode.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BK7258_OTA_CONTROL_PATH_SIZE 128u
#define BK7258_OTA_CONTROL_URL_SIZE  256u
#define BK7258_OTA_CONTROL_CA_SIZE   128u

#ifdef CONFIG_BK7258_OTA_RPMSG
int bk7258_ota_rpmsg_initialize(void);

#ifdef CONFIG_BK7258_AP_CORE
/* Open/read/checkpoint/cancel/close run on AP.  This call serializes one
 * complete session, proxies CP read_at requests to source, and returns the CP
 * Pair Installer result. */

int bk7258_ota_rpmsg_stage(const struct bk7258_ota_source_ops_s *source,
                           void *context, uint32_t timeout_ms);
int bk7258_ota_rpmsg_cancel(void);
#else
/* CP maintenance/control adapter.  These calls never open files or perform
 * source I/O on CP; they ask the AP OTA Manager to do so and wait for its
 * bounded reply. */

int bk7258_ota_rpmsg_apply_file(const char *root, uint32_t timeout_ms);
int bk7258_ota_rpmsg_apply_http(const char *catalog_url,
                                const char *ca_path,
                                uint32_t timeout_ms);
int bk7258_ota_rpmsg_manager_status(
  struct bk7258_ota_manager_status_s *status, uint32_t timeout_ms);
int bk7258_ota_rpmsg_manager_cancel(uint32_t timeout_ms);
int bk7258_ota_rpmsg_usbmode_get(enum bk7258_usbmode_e *mode,
                                 uint32_t timeout_ms);
int bk7258_ota_rpmsg_usbmode_set(enum bk7258_usbmode_e mode,
                                 enum bk7258_usbmode_e *actual,
                                 uint32_t timeout_ms);
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_RPMSG_H */
