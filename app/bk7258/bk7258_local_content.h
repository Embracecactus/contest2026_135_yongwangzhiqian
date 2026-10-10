/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BK7258_LOCAL_CONTENT_H
#define BK7258_LOCAL_CONTENT_H

#include <stdbool.h>
#include <stdint.h>

enum bkcontent_phase_e
{
  BKCONTENT_IDLE,
  BKCONTENT_PENDING,
  BKCONTENT_READY,
  BKCONTENT_PLAYING,
  BKCONTENT_DONE,
  BKCONTENT_FAILED,
  BKCONTENT_CANCELED
};

struct bkcontent_status_s
{
  uint32_t id;
  unsigned int phase;
  unsigned int content;
  int error;
};

/* Metadata-only ingress. Authorization belongs to the existing owner route.
 * The product owner releases capture before ready; the existing outbound
 * worker performs file/player I/O. No new thread or unbounded job queue.
 */

int bkcontent_submit(unsigned int action, uint64_t content, uint32_t *id);
int bkcontent_ready(uint32_t id);
int bkcontent_cancel(uint32_t id);
void bkcontent_status(struct bkcontent_status_s *status);
bool bkcontent_busy(void);
void bkcontent_step(bool admitted);
int bkcontent_quiesce(void);
void bkcontent_work(void);

#endif
