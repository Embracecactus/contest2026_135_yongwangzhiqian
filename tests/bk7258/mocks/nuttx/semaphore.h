/****************************************************************************
 * tests/mocks/nuttx/semaphore.h
 *
 * Host shim mapping the NuttX nxsem_* API onto POSIX semaphores.  The
 * implementation lives in mock_sdk.c; only the ABI is declared here.
 *
 * nxsem_tickwait_uninterruptible(sem, ticks) blocks up to `ticks`
 * milliseconds and returns 0 on success or -ETIMEDOUT on timeout.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_SEMAPHORE_H
#define __MOCK_NUTTX_SEMAPHORE_H

#include <semaphore.h>
#include <stdint.h>

/* sem_t is provided by <semaphore.h>; reuse it directly. */
typedef sem_t sem_t;

#define SEM_INITIALIZER(c) { 0 }

int nxsem_init(sem_t *sem, int pshared, unsigned int value);
int nxsem_destroy(sem_t *sem);
int nxsem_post(sem_t *sem);
int nxsem_trywait(sem_t *sem);
int nxsem_tickwait_uninterruptible(sem_t *sem, int ticks);
int nxsem_wait_uninterruptible(sem_t *sem);

#endif /* __MOCK_NUTTX_SEMAPHORE_H */
