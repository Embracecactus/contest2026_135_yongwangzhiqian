/****************************************************************************
 * tests/mocks/nuttx/spinlock.h
 *
 * Host shim: a spinlock is just a pthread mutex (single-process, so no
 * cross-CPU exclusion is needed).  irqstate_t is a placeholder return value
 * that spin_unlock_irqrestore() ignores.
 *
 * The real code only ever briefly holds this lock, and never across the
 * nxsem_tickwait() call, so a non-recursive mutex cannot deadlock with the
 * worker thread.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_SPINLOCK_H
#define __MOCK_NUTTX_SPINLOCK_H

#include <pthread.h>

typedef pthread_mutex_t spinlock_t;
#define SP_UNLOCKED     PTHREAD_MUTEX_INITIALIZER

typedef unsigned long irqstate_t;

static inline irqstate_t spin_lock_irqsave(spinlock_t *lock)
{
  pthread_mutex_lock(lock);
  return 0;
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, irqstate_t flags)
{
  (void)flags;
  pthread_mutex_unlock(lock);
}

/* Host stand-ins for the NuttX critical-section helpers (single-threaded
 * test process, so interrupt disable is a no-op). */

static inline irqstate_t enter_critical_section(void)
{
  return 0;
}

static inline void leave_critical_section(irqstate_t flags)
{
  (void)flags;
}

#endif /* __MOCK_NUTTX_SPINLOCK_H */
