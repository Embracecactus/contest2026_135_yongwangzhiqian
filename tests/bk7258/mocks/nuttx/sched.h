/****************************************************************************
 * tests/mocks/nuttx/sched.h
 *
 * Host shim.  We deliberately do NOT define CONFIG_SMP, so the .c never
 * references cpu_set_t / sched_setaffinity; only pid_t and
 * INVALID_PROCESS_ID are needed.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_SCHED_H
#define __MOCK_NUTTX_SCHED_H

#include <sys/types.h>

#ifndef INVALID_PROCESS_ID
#define INVALID_PROCESS_ID ((pid_t)-1)
#endif

pid_t nxsched_gettid(void);

#endif /* __MOCK_NUTTX_SCHED_H */
