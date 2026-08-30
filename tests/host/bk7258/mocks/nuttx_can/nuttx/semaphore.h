/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/host/bk7258/mocks/nuttx_can/nuttx/semaphore.h
 *
 * Host shim for the NuttX nxsem_* surface used by bk7258_can.c.
 *
 * The CAN lower-half runs a real NuttX kernel thread that blocks in
 * nxsem_wait() until the SDK ISR callbacks post it.  Unlike the
 * deterministic single-threaded yuv/scale suites, this one needs REAL
 * blocking, so sem_t is a counting semaphore built on pthread mutex+cond
 * and the implementation lives in mock_sdk_can.c.  SEM_INITIALIZER(c)
 * must produce a valid static initializer (the driver statically
 * initializes its rx_sem with SEM_INITIALIZER(0)).
 *
 * This header shadows mocks/nuttx/semaphore.h for the three CAN TUs via
 * -I mocks/nuttx_can (before -I mocks); every other suite keeps the
 * POSIX-sem based shared mock untouched.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_CAN_NUTTX_SEMAPHORE_H
#define __MOCK_NUTTX_CAN_NUTTX_SEMAPHORE_H

#include <pthread.h>
#include <stdint.h>

struct mock_can_sem_s
{
  pthread_mutex_t lock;
  pthread_cond_t cond;
  unsigned int count;
};

typedef struct mock_can_sem_s sem_t;

#define SEM_INITIALIZER(c)                                              \
  {                                                                     \
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, (c)            \
  }

int nxsem_init(sem_t *sem, int pshared, unsigned int value);
int nxsem_destroy(sem_t *sem);
int nxsem_post(sem_t *sem);
int nxsem_wait(sem_t *sem);
int nxsem_reset(sem_t *sem, unsigned int value);

#endif /* __MOCK_NUTTX_CAN_NUTTX_SEMAPHORE_H */
