/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/mocks/nuttx/kthread.h
 *
 * Host shim: a "kthread" is a detached POSIX thread.  The implementation is
 * in mock_sdk.c.  kthread_delete() is a no-op stub here (the test harness
 * stops the worker explicitly via mock_mbox_fini()).
 ****************************************************************************/

#ifndef __MOCK_NUTTX_KTHREAD_H
#define __MOCK_NUTTX_KTHREAD_H

#include <sys/types.h>

#ifndef SCHED_PRIORITY_DEFAULT
#define SCHED_PRIORITY_DEFAULT 100
#endif

pid_t kthread_create(const char *name, int priority, int stacksize,
                     int (*entry)(int, char *argv[]), char *argv[]);
int kthread_delete(pid_t pid);

#endif /* __MOCK_NUTTX_KTHREAD_H */
