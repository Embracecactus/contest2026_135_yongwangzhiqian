/*
 * mock_sem.c - host NuttX nxsem_* shim (POSIX-backed) for the IrDA suite.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The yuv/scale suites provide their own deterministic nxsem_* variants in
 * mock_sdk_yuv_h264.c / mock_sdk_scale_rotate.c, so this TU is linked only
 * by tests that need the plain POSIX-backed blocking semantics (IrDA).
 */
#include "nuttx/semaphore.h"

#include <errno.h>
#include <semaphore.h>

int nxsem_init(sem_t *sem, int pshared, unsigned int value)
{
  return sem_init(sem, pshared, value);
}

int nxsem_destroy(sem_t *sem)
{
  return sem_destroy(sem);
}

int nxsem_post(sem_t *sem)
{
  return sem_post(sem);
}

int nxsem_wait_uninterruptible(sem_t *sem)
{
  return sem_wait(sem) == 0 ? 0 : -errno;
}
