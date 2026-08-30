/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __MOCK_NUTTX_RPTUN_RPTUN_H
#define __MOCK_NUTTX_RPTUN_RPTUN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RPTUN_NOTIFY_ALL       UINT32_MAX
#define RSC_VDEV               3u
#define RSC_CARVEOUT           0u
#define VIRTIO_ID_RPMSG        7u
#define VIRTIO_DEV_DRIVER      1u
#define FW_RSC_U32_ADDR_ANY    UINT32_MAX

#define VIRTIO_RPMSG_F_NS      0u
#define VIRTIO_RPMSG_F_ACK     1u
#define VIRTIO_RPMSG_F_BUFSZ   2u
#define VIRTIO_RPMSG_F_CPUNAME 3u

struct resource_table
{
  uint32_t ver;
  uint32_t num;
  uint32_t reserved[2];
};

struct mock_rsc_vdev_s
{
  uint32_t type;
  uint32_t id;
  uint32_t notifyid;
  uint32_t dfeatures;
  uint32_t gfeatures;
  uint32_t config_len;
  uint8_t status;
  uint8_t num_of_vrings;
  uint8_t reserved[2];
};

struct mock_rsc_vring_s
{
  uint32_t da;
  uint32_t align;
  uint32_t num;
  uint32_t notifyid;
  uint32_t reserved;
};

struct fw_rsc_config
{
  uint32_t r2h_buf_size;
  uint32_t h2r_buf_size;
  char host_cpuname[16];
  char remote_cpuname[16];
};

struct mock_rsc_carveout_s
{
  uint32_t type;
  uint32_t da;
  uint32_t pa;
  uint32_t len;
  uint32_t flags;
  uint32_t reserved;
  char name[32];
};

struct rptun_rsc_s
{
  struct resource_table rsc_tbl_hdr;
  uint32_t offset[2];
  struct mock_rsc_vdev_s rpmsg_vdev;
  struct mock_rsc_vring_s rpmsg_vring0;
  struct mock_rsc_vring_s rpmsg_vring1;
  struct fw_rsc_config config;
  struct mock_rsc_carveout_s carveout;
};

struct rptun_dev_s;
typedef void (*rptun_callback_t)(void *arg, uint32_t vqid);

struct rptun_ops_s
{
  const char *(*get_cpuname)(struct rptun_dev_s *dev);
  struct resource_table *(*get_resource)(struct rptun_dev_s *dev);
  bool (*is_autostart)(struct rptun_dev_s *dev);
  bool (*is_master)(struct rptun_dev_s *dev);
  int (*config)(struct rptun_dev_s *dev, void *data);
  int (*start)(struct rptun_dev_s *dev);
  int (*stop)(struct rptun_dev_s *dev);
  int (*notify)(struct rptun_dev_s *dev, uint32_t vqid);
  int (*register_callback)(struct rptun_dev_s *dev,
                           rptun_callback_t callback, void *arg);
};

struct rptun_dev_s
{
  const struct rptun_ops_s *ops;
  void *stack;
  size_t stack_size;
};

int rptun_initialize(struct rptun_dev_s *dev);
int rptun_boot(const char *cpuname);
int rptun_poweroff(const char *cpuname);

#endif /* __MOCK_NUTTX_RPTUN_RPTUN_H */
