/****************************************************************************
 * app/vela_claw/include/claw_log.h
 *
 * Logging macros. On NuttX they route to syslog; on the host they go to
 * stderr. Intentionally tiny so the core stays dependency-free and host
 * unit-testable.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_LOG_H
#define VELA_CLAW_LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __NuttX__
#include <syslog.h>
#define CLAW_LOGD(fmt, ...) syslog(LOG_DEBUG,    "VCLAW: " fmt, ##__VA_ARGS__)
#define CLAW_LOGI(fmt, ...) syslog(LOG_INFO,     "VCLAW: " fmt, ##__VA_ARGS__)
#define CLAW_LOGW(fmt, ...) syslog(LOG_WARNING,  "VCLAW: " fmt, ##__VA_ARGS__)
#define CLAW_LOGE(fmt, ...) syslog(LOG_ERR,      "VCLAW: " fmt, ##__VA_ARGS__)
#else
#define CLAW_LOGD(fmt, ...) fprintf(stderr, "[D] " fmt "\n", ##__VA_ARGS__)
#define CLAW_LOGI(fmt, ...) fprintf(stderr, "[I] " fmt "\n", ##__VA_ARGS__)
#define CLAW_LOGW(fmt, ...) fprintf(stderr, "[W] " fmt "\n", ##__VA_ARGS__)
#define CLAW_LOGE(fmt, ...) fprintf(stderr, "[E] " fmt "\n", ##__VA_ARGS__)
#endif

#ifdef __cplusplus
}
#endif

#endif /* VELA_CLAW_LOG_H */
