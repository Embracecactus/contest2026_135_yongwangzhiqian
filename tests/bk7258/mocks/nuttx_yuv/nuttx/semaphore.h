/****************************************************************************
 * tests/mocks/nuttx_yuv/semaphore.h
 *
 * YUV suite variant of the NuttX semaphore shim (replaces
 * mocks/nuttx/semaphore.h for the bk7258_yuv_h264 build only, via -I
 * mocks/nuttx_yuv ahead of -I mocks).
 *
 * Host shim mapping the NuttX nxsem_* API onto a tiny counting semaphore.
 * A static initializer is required by the AP media driver singletons, which
 * POSIX sem_t cannot supply, so sem_t is a plain counter and every nxsem_*
 * call is deterministic (no real blocking).
 *
 * nxsem_tickwait_uninterruptible(sem, ticks): with count > 0 it consumes one
 * token and returns 0; otherwise it runs the optional suite-controlled
 * tickwait hook (which may post tokens by firing captured SDK ISRs) and
 * returns 0 if a token appeared, else -ETIMEDOUT.  The hook allows the
 * block-style event loops of the AP media drivers to be driven
 * single-threaded and exactly once per wait.
 *
 * The implementation lives in mock_sdk_yuv_h264.c; only the ABI is declared
 * here.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_YUV_SEMAPHORE_H
#define __MOCK_NUTTX_YUV_SEMAPHORE_H

#include <stdint.h>

typedef struct sem_s
{
  int count;
} sem_t;

#define SEM_INITIALIZER(c) { (c) }

int nxsem_init(sem_t *sem, int pshared, unsigned int value);
int nxsem_destroy(sem_t *sem);
int nxsem_reset(sem_t *sem, unsigned int count);
int nxsem_post(sem_t *sem);
int nxsem_trywait(sem_t *sem);
int nxsem_tickwait_uninterruptible(sem_t *sem, int ticks);

#endif /* __MOCK_NUTTX_YUV_SEMAPHORE_H */