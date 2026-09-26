/****************************************************************************
 * app/bk7258/bk7258_nfc_service.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APP_BK7258_BK7258_NFC_SERVICE_H
#define __APP_BK7258_BK7258_NFC_SERVICE_H
#include <stdbool.h>
/* 只关闭准入并读取退出状态，不在调用线程执行设备I/O。 */
int bk7258_nfc_service_quiesce(bool stop);
/* 明确的新停止意图才重试失败的RF释放；仍由原worker执行。 */
int bk7258_nfc_service_retry_stop(void);
int bk7258_nfc_service_prepare(void);
int bk7258_nfc_service_start(void);
#endif /* __APP_BK7258_BK7258_NFC_SERVICE_H */
