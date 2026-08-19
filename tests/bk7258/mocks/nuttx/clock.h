/****************************************************************************
 * tests/mocks/nuttx/clock.h
 *
 * Minimal clock shim for the host build.  MSEC2TICK() maps milliseconds
 * 1:1 onto the integer "tick" argument that nxsem_tickwait_uninterruptible
 * expects, so the 1 ms worker poll becomes a 1 ms host timeout.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_CLOCK_H
#define __MOCK_NUTTX_CLOCK_H

#include <stdint.h>
#include <time.h>

/* clock_t is provided by the host <sys/types.h> (included before this
 * header by the implementation); do not redefine it here. */

#define MSEC2TICK(ms) ((clock_t)(ms))

int clock_systime_timespec(struct timespec *ts);

#endif /* __MOCK_NUTTX_CLOCK_H */
