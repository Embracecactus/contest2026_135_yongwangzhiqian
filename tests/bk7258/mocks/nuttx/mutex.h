/****************************************************************************
 * tests/mocks/nuttx/mutex.h
 *
 * Host shim for the NuttX mutex surface used by the AP board helpers.
 * nxmutex_lock() returns 0 or a negative errno, matching NuttX.
 * mock_mutex_fail_next(n) makes the next n lock attempts fail with -EAGAIN
 * so the "lock failure" plumbing of the driver is exercised.  The
 * implementation lives in mock_nuttx_ap.c.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_MUTEX_H
#define __MOCK_NUTTX_MUTEX_H

#include <pthread.h>

#ifndef FAR
#define FAR
#endif

typedef pthread_mutex_t mutex_t;
#define NXMUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER

int nxmutex_lock(FAR mutex_t *mutex);
int nxmutex_timedlock(FAR mutex_t *mutex, unsigned int timeout_ms);
int nxmutex_unlock(FAR mutex_t *mutex);

/* Test control. */
void mock_mutex_fail_next(int failures);

#endif /* __MOCK_NUTTX_MUTEX_H */
