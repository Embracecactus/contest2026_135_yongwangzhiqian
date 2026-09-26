/****************************************************************************
 * app/bk7258/bk7258_nfc_service.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APP_BK7258_BK7258_NFC_SERVICE_H
#define __APP_BK7258_BK7258_NFC_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
/* 只关闭准入并读取退出状态，不在调用线程执行设备I/O。 */
int bk7258_nfc_service_quiesce(bool stop);
/* 明确的新停止意图才重试失败的RF释放；仍由原worker执行。 */
int bk7258_nfc_service_retry_stop(void);
#ifdef CONFIG_BK7258_PROVISION_GATT
enum bknfc_job_action_e { BKNFC_JOB_LOAD = 1, BKNFC_JOB_ENROLL, BKNFC_JOB_REMOVE };
enum bknfc_job_phase_e { BKNFC_JOB_IDLE, BKNFC_JOB_PENDING, BKNFC_JOB_RUNNING,
  BKNFC_JOB_COMMITTING, BKNFC_JOB_SUCCEEDED, BKNFC_JOB_FAILED,
  BKNFC_JOB_CANCELED, BKNFC_JOB_UNKNOWN };
struct bknfc_job_request_s
{
  uint64_t operation;
  uint64_t revision;
  uint64_t duration_ms;
  unsigned int action;
  unsigned int slot;
};
struct bknfc_job_status_s
{
  uint64_t operation;
  uint64_t revision;
  uint64_t durations[8];
  unsigned int phase;
  int error;
};
/* 仅内部已认证产品适配器调用；查询不做I/O且不导出UID。 */
int bk7258_nfc_job_submit(const struct bknfc_job_request_s *request);
int bk7258_nfc_job_cancel(uint64_t operation);
void bk7258_nfc_job_status(struct bknfc_job_status_s *status);
/* 已授权重置工作者：NFC退出后独占清理并使缓存失效，不恢复准入。 */
int bk7258_nfc_bindings_reset(void);
#endif
int bk7258_nfc_service_prepare(void);
int bk7258_nfc_service_start(void);
#endif /* __APP_BK7258_BK7258_NFC_SERVICE_H */
