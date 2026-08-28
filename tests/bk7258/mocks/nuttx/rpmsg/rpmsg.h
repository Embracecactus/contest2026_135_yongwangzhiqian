/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __MOCK_NUTTX_RPMSG_RPMSG_H
#define __MOCK_NUTTX_RPMSG_RPMSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RPMSG_NAME_SIZE 32
#define RPMSG_ADDR_ANY  UINT32_MAX

struct rpmsg_device;
struct rpmsg_endpoint;

typedef int (*rpmsg_ept_cb_t)(struct rpmsg_endpoint *ept, void *data,
                              size_t len, uint32_t src, void *priv);
typedef void (*rpmsg_ns_bound_cb_t)(struct rpmsg_endpoint *ept);
typedef void (*rpmsg_ns_unbind_cb_t)(struct rpmsg_endpoint *ept);
typedef void (*rpmsg_dev_cb_t)(struct rpmsg_device *rdev, void *priv);
typedef bool (*rpmsg_match_cb_t)(struct rpmsg_device *rdev, void *priv,
                                 const char *name, uint32_t dest);
typedef void (*rpmsg_bind_cb_t)(struct rpmsg_device *rdev, void *priv,
                                const char *name, uint32_t dest);

struct rpmsg_device
{
  const char *cpuname;
  bool support_ns;
  bool support_ack;
};

struct rpmsg_endpoint
{
  struct rpmsg_device *rdev;
  void *priv;
  char name[RPMSG_NAME_SIZE];
  uint32_t addr;
  uint32_t dest_addr;
  rpmsg_ns_bound_cb_t ns_bound_cb;
};

static inline bool is_rpmsg_ept_ready(struct rpmsg_endpoint *ept)
{
  return ept != NULL && ept->rdev != NULL &&
         ept->dest_addr != RPMSG_ADDR_ANY;
}

const char *rpmsg_get_cpuname(struct rpmsg_device *rdev);
int rpmsg_create_ept(struct rpmsg_endpoint *ept,
                     struct rpmsg_device *rdev, const char *name,
                     uint32_t src, uint32_t dest, rpmsg_ept_cb_t cb,
                     rpmsg_ns_unbind_cb_t unbind_cb);
void rpmsg_destroy_ept(struct rpmsg_endpoint *ept);
int rpmsg_register_callback(void *priv,
                            rpmsg_dev_cb_t device_created,
                            rpmsg_dev_cb_t device_destroy,
                            rpmsg_match_cb_t ns_match,
                            rpmsg_bind_cb_t ns_bind);
void rpmsg_unregister_callback(void *priv,
                               rpmsg_dev_cb_t device_created,
                               rpmsg_dev_cb_t device_destroy,
                               rpmsg_match_cb_t ns_match,
                               rpmsg_bind_cb_t ns_bind);

#endif /* __MOCK_NUTTX_RPMSG_RPMSG_H */
