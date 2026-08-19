/****************************************************************************
 * app/vela_claw/include/claw_rtos.h
 *
 * Pure-POSIX RTOS abstraction. The whole Vela-Claw core depends only on this
 * surface (threads, mutexes, semaphores, a thread-safe queue) so it compiles
 * unchanged on NuttX and on the host. The NuttX port uses the POSIX-layer
 * pthread/sem backends; the host uses glibc.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_RTOS_H
#define VELA_CLAW_RTOS_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>

#include "claw_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef pthread_t  claw_thread_t;
typedef pthread_mutex_t claw_mutex_t;
typedef sem_t     claw_sem_t;
typedef struct claw_queue_s claw_queue_t;

/* ---- thread ---- */
claw_err_t claw_thread_create(claw_thread_t *tid, void *(*entry)(void *),
                              void *arg, int priority, int stack_bytes);
void       claw_thread_join(claw_thread_t tid);
void       claw_thread_detach(claw_thread_t tid);

/* ---- mutex ---- */
claw_err_t claw_mutex_init(claw_mutex_t *m);
claw_err_t claw_mutex_lock(claw_mutex_t *m);
claw_err_t claw_mutex_unlock(claw_mutex_t *m);
claw_err_t claw_mutex_destroy(claw_mutex_t *m);

/* ---- semaphore ---- */
claw_err_t claw_sem_init(claw_sem_t *s, unsigned value);
claw_err_t claw_sem_wait(claw_sem_t *s);
claw_err_t claw_sem_trywait(claw_sem_t *s);
claw_err_t claw_sem_post(claw_sem_t *s);
claw_err_t claw_sem_destroy(claw_sem_t *s);

/* ---- thread-safe queue ----
 * timeout_ms == 0  -> non-blocking (return CLAW_EAGAIN if empty)
 * timeout_ms  > 0  -> block up to timeout_ms (return CLAW_ETIMEDOUT on host
 *                     if not available; on NuttX it blocks until available)
 * msg is copied (msg_size bytes) into an internal node.
 */
claw_queue_t *claw_queue_create(size_t max_msgs, size_t msg_size);
void         claw_queue_destroy(claw_queue_t *q);
claw_err_t   claw_queue_push(claw_queue_t *q, const void *msg, int timeout_ms);
claw_err_t   claw_queue_pop(claw_queue_t *q, void *msg, int timeout_ms);

/* ---- time ---- */
void    claw_sleep_ms(unsigned ms);
uint64_t claw_now_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* VELA_CLAW_RTOS_H */
