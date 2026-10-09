/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __TEST_PM_REPLAY_NUTTX_RPMSG_RPMSG_H
#define __TEST_PM_REPLAY_NUTTX_RPMSG_RPMSG_H

#include_next <nuttx/rpmsg/rpmsg.h>

#ifndef RPMSG_ERR_NO_BUFF
#  define RPMSG_ERR_NO_BUFF (-12)
#endif

int rpmsg_trysend(struct rpmsg_endpoint *ept, const void *data, int len);

#endif
