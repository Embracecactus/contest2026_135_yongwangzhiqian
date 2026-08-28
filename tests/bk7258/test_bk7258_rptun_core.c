/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/test_bk7258_rptun_core.c
 *
 * Host lifecycle test for the chip-owned, service-independent RPMsg Name
 * Service proof in bk7258_rptun.c.  The same source is built once as CP and
 * once as AP so CREATE -> bind/ACK -> ACK receipt and restart cleanup are
 * checked without enabling RPMSG_TEST or the AP supervisor.
 ****************************************************************************/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/chip/bk7258_rptun.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/rptun/rptun.h>

#include "bk7258_rptun.h"
#include "bk7258_rptun_mbox.h"

static int g_checks;
static int g_failures;

#define CHECK(condition, ...)                                               \
  do                                                                        \
    {                                                                       \
      g_checks++;                                                           \
      if (!(condition))                                                     \
        {                                                                   \
          g_failures++;                                                     \
          fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);             \
          fprintf(stderr, __VA_ARGS__);                                     \
          fputc('\n', stderr);                                              \
        }                                                                   \
    }                                                                       \
  while (0)

unsigned char g_mock_rptun_resource[512] __attribute__((aligned(8)));
unsigned char g_mock_rptun_carveout[1024] __attribute__((aligned(8)));

static struct bk7258_rptun_control_s g_control;
static struct rptun_dev_s *g_lower;
static struct rpmsg_device g_remote =
{
#ifdef TEST_BK7258_RPTUN_AP
  .cpuname = "cp",
#else
  .cpuname = "ap",
#endif
  .support_ns = true,
  .support_ack = true,
};

static void *g_rpmsg_priv;
static rpmsg_dev_cb_t g_device_created;
static rpmsg_dev_cb_t g_device_destroyed;
static rpmsg_match_cb_t g_ns_match;
static rpmsg_bind_cb_t g_ns_bind;
static struct rpmsg_endpoint *g_last_ept;
static rptun_callback_t g_upper_callback;
static void *g_upper_arg;
static bk7258_rptun_notify_t g_mbox_notify;

static int g_order;
static int g_register_order;
static int g_rptun_order;
static int g_register_count;
static int g_unregister_count;
static int g_create_count;
static int g_destroy_count;
static int g_ack_count;
static int g_boot_count;
static int g_poweroff_count;
static uint32_t g_upper_vqid;
static unsigned int g_upper_count;

volatile struct bk7258_rptun_control_s *bk7258_rptun_control(void)
{
  return &g_control;
}

int nxsig_usleep(uint32_t usec)
{
  (void)usec;
  return 0;
}

const char *rpmsg_get_cpuname(struct rpmsg_device *rdev)
{
  return rdev->cpuname;
}

int rpmsg_create_ept(struct rpmsg_endpoint *ept,
                     struct rpmsg_device *rdev, const char *name,
                     uint32_t src, uint32_t dest, rpmsg_ept_cb_t callback,
                     rpmsg_ns_unbind_cb_t unbind_cb)
{
  (void)callback;
  (void)unbind_cb;
  ept->rdev = rdev;
  ept->addr = src == RPMSG_ADDR_ANY ? 1024u + (uint32_t)g_create_count : src;
  ept->dest_addr = dest;
  snprintf(ept->name, sizeof(ept->name), "%s", name);
  g_last_ept = ept;
  g_create_count++;
  if (dest != RPMSG_ADDR_ANY && rdev->support_ack)
    {
      g_ack_count++;
    }

  return 0;
}

void rpmsg_destroy_ept(struct rpmsg_endpoint *ept)
{
  g_destroy_count++;
  ept->rdev = NULL;
  ept->name[0] = '\0';
  ept->addr = RPMSG_ADDR_ANY;
  ept->dest_addr = RPMSG_ADDR_ANY;
}

int rpmsg_register_callback(void *priv,
                            rpmsg_dev_cb_t device_created,
                            rpmsg_dev_cb_t device_destroy,
                            rpmsg_match_cb_t ns_match,
                            rpmsg_bind_cb_t ns_bind)
{
  g_rpmsg_priv = priv;
  g_device_created = device_created;
  g_device_destroyed = device_destroy;
  g_ns_match = ns_match;
  g_ns_bind = ns_bind;
  g_register_count++;
  g_register_order = ++g_order;
  return 0;
}

void rpmsg_unregister_callback(void *priv,
                               rpmsg_dev_cb_t device_created,
                               rpmsg_dev_cb_t device_destroy,
                               rpmsg_match_cb_t ns_match,
                               rpmsg_bind_cb_t ns_bind)
{
  CHECK(priv == g_rpmsg_priv && device_created == g_device_created &&
        device_destroy == g_device_destroyed && ns_match == g_ns_match &&
        ns_bind == g_ns_bind, "unregister must match the proof callback");
  g_unregister_count++;
}

static void test_upper_callback(void *arg, uint32_t vqid)
{
  CHECK(arg == &g_control, "upper callback argument is preserved");
  g_upper_vqid = vqid;
  g_upper_count++;
}

int rptun_initialize(struct rptun_dev_s *dev)
{
  g_lower = dev;
  g_rptun_order = ++g_order;
  if (dev->ops->is_master(dev))
    {
      CHECK(dev->ops->start(dev) == 0, "CP lower-half start succeeds");
    }

  g_upper_callback = test_upper_callback;
  g_upper_arg = &g_control;
  return dev->ops->register_callback(dev, g_upper_callback, g_upper_arg);
}

int rptun_boot(const char *cpuname)
{
  CHECK(g_lower != NULL && strcmp(cpuname, "ap") == 0,
        "CP restart targets AP");
  g_boot_count++;
  CHECK(g_lower->ops->register_callback(g_lower, NULL, NULL) == 0,
        "restart unregisters the old lower callback");
  CHECK(g_lower->ops->start(g_lower) == 0,
        "restart transitions TABLE_READY to CONNECTING");
  return g_lower->ops->register_callback(g_lower, g_upper_callback,
                                         g_upper_arg);
}

int rptun_poweroff(const char *cpuname)
{
  CHECK(g_lower != NULL && strcmp(cpuname, "ap") == 0,
        "CP poweroff targets AP");
  g_poweroff_count++;

  /* Match NuttX rptun_dev_stop(): remove RPMsg devices, unregister the
   * mailbox callback, then remoteproc_shutdown() invokes the lower stop.
   */

  if (g_device_destroyed != NULL && g_last_ept != NULL &&
      g_last_ept->rdev == &g_remote)
    {
      g_device_destroyed(&g_remote, g_rpmsg_priv);
    }

  CHECK(g_lower->ops->register_callback(g_lower, NULL, NULL) == 0,
        "poweroff unregisters the lower callback");
  return g_lower->ops->stop(g_lower);
}

int bk7258_rptun_mbox_notify(uint32_t generation, uint32_t value)
{
  (void)generation;
  (void)value;
  return 0;
}

void bk7258_rptun_mbox_set_notify(bk7258_rptun_notify_t callback)
{
  g_mbox_notify = callback;
}

static void initialize_control(uint32_t generation)
{
  memset(&g_control, 0, sizeof(g_control));
  memset(g_mock_rptun_resource, 0xa5, sizeof(g_mock_rptun_resource));
  memset(g_mock_rptun_carveout, 0x5a, sizeof(g_mock_rptun_carveout));
  g_control.magic = BK7258_RPTUN_CONTROL_MAGIC;
  g_control.version = BK7258_RPTUN_CONTROL_VERSION;
  g_control.size = sizeof(g_control);
  g_control.generation = generation;
#ifdef TEST_BK7258_RPTUN_AP
  g_control.state = BK7258_RPTUN_STATE_CONNECTING;
#endif
}

static void expect_bootstrap(uint32_t generation, uint32_t notify,
                             uint32_t expected)
{
  unsigned int before = g_upper_count;

  CHECK(g_mbox_notify != NULL, "lower mailbox callback is installed");
  g_mbox_notify(generation, notify);
  CHECK(g_upper_count == before + 1, "one upper notification is delivered");
  CHECK(g_upper_vqid == expected,
        "upper notification is 0x%08x (got 0x%08x)", expected,
        g_upper_vqid);
}

#ifdef TEST_BK7258_RPTUN_AP
static void test_ap_create_ack_receipt_and_destroy(void)
{
  initialize_control(1);

  CHECK(bk7258_rptun_initialize(1) == 0, "AP RPTUN initializes");
  CHECK(g_register_count == 1, "proof callback registers exactly once");
  CHECK(g_register_order < g_rptun_order,
        "proof callback registers before asynchronous RPTUN start");
  CHECK((g_control.flags & BK7258_RPTUN_FLAG_AP_CORE_READY) != 0,
        "AP publishes AP_CORE_READY");
  CHECK(g_ns_match == NULL && g_ns_bind == NULL,
        "AP is the active NS CREATE side");

  expect_bootstrap(1, BK7258_RPTUN_NOTIFY_VRING0, RPTUN_NOTIFY_ALL);
  g_remote.support_ack = false;
  g_device_created(&g_remote, g_rpmsg_priv);
  CHECK(g_control.state == BK7258_RPTUN_STATE_FAULTED &&
        g_control.error == EPROTONOSUPPORT,
        "missing ACK support fails the CONNECTING proof closed");
  g_control.state = BK7258_RPTUN_STATE_CONNECTING;
  g_control.error = 0;
  g_remote.support_ack = true;
  g_device_created(&g_remote, g_rpmsg_priv);
  CHECK(g_create_count == 1, "AP creates one proof endpoint");
  CHECK(g_last_ept != NULL &&
        strcmp(g_last_ept->name, "bk7258-rptun-00000001") == 0,
        "AP proof name carries generation");
  CHECK(g_last_ept->dest_addr == RPMSG_ADDR_ANY,
        "AP CREATE starts with an unknown destination");
  CHECK(g_last_ept->ns_bound_cb != NULL,
        "AP installs ACK receipt callback before CREATE");
  CHECK(g_control.state == BK7258_RPTUN_STATE_CONNECTING,
        "CREATE alone is not a bidirectional proof");

  g_last_ept->dest_addr = 0x501u;
  g_control.generation = 2;
  g_last_ept->ns_bound_cb(g_last_ept);
  CHECK(g_control.state == BK7258_RPTUN_STATE_CONNECTING,
        "stale-generation ACK cannot mark the new lifecycle connected");
  expect_bootstrap(1, BK7258_RPTUN_NOTIFY_VRING1, RPTUN_NOTIFY_ALL);

  g_device_destroyed(&g_remote, g_rpmsg_priv);
  CHECK(g_destroy_count == 1, "destroy closes an unbound AP proof endpoint");
  g_control.generation = 1;
  g_device_created(&g_remote, g_rpmsg_priv);
  CHECK(g_create_count == 2, "same-generation device recreation is supported");
  g_last_ept->dest_addr = 0x502u;
  g_last_ept->ns_bound_cb(g_last_ept);

  CHECK(g_control.state == BK7258_RPTUN_STATE_CONNECTED,
        "AP ACK receipt marks CONNECTED");
  CHECK((g_control.flags & BK7258_RPTUN_FLAG_CONNECTED_ONCE) != 0,
        "AP ACK receipt preserves CONNECTED_ONCE");
  expect_bootstrap(1, BK7258_RPTUN_NOTIFY_VRING1, 1u);

  g_remote.support_ack = false;
  g_device_created(&g_remote, g_rpmsg_priv);
  CHECK(g_control.state == BK7258_RPTUN_STATE_CONNECTED &&
        g_control.error == 0,
        "late duplicate callback cannot pollute a CONNECTED state");
  g_remote.support_ack = true;

  g_device_destroyed(&g_remote, g_rpmsg_priv);
  CHECK(g_destroy_count == 2, "connected endpoint is destroyed with the rdev");
  CHECK(bk7258_rptun_initialize(2) == -ESTALE,
        "running AP lower-half rejects an in-place generation change");
  CHECK(g_unregister_count == 0,
        "proof callback remains registered for the RPTUN device lifetime");
}
#else
static void test_cp_bind_ack_and_restart(void)
{
  const char *name1 = "bk7258-rptun-00000001";
  const char *name2 = "bk7258-rptun-00000002";

  initialize_control(1);

  CHECK(bk7258_rptun_initialize(1) == 0, "CP RPTUN initializes");
  CHECK(g_register_count == 1, "proof callback registers exactly once");
  CHECK(g_register_order < g_rptun_order,
        "proof callback registers before asynchronous RPTUN start");
  CHECK(g_control.state == BK7258_RPTUN_STATE_CONNECTING,
        "CP lower start publishes CONNECTING");
  CHECK(g_ns_match != NULL && g_ns_bind != NULL,
        "CP is the passive NS bind/ACK side");
  g_device_created(&g_remote, g_rpmsg_priv);
  CHECK(g_create_count == 0, "CP device creation does not announce a service");

  expect_bootstrap(1, BK7258_RPTUN_NOTIFY_VRING0, RPTUN_NOTIFY_ALL);
  g_remote.support_ack = false;
  g_device_created(&g_remote, g_rpmsg_priv);
  CHECK(g_control.state == BK7258_RPTUN_STATE_FAULTED &&
        g_control.error == EPROTONOSUPPORT,
        "missing ACK support fails the CONNECTING proof closed");
  g_control.state = BK7258_RPTUN_STATE_CONNECTING;
  g_control.error = 0;
  g_remote.support_ack = true;
  CHECK(!g_ns_match(&g_remote, g_rpmsg_priv,
                    "bk7258-rptun-ffffffff", 0x401u),
        "CP rejects a proof name from another generation");
  g_control.generation = 2;
  CHECK(!g_ns_match(&g_remote, g_rpmsg_priv, name1, 0x401u),
        "CP rejects CREATE while shared generation is stale");
  g_control.generation = 1;
  CHECK(g_ns_match(&g_remote, g_rpmsg_priv, name1, 0x401u),
        "CP accepts the current-generation AP CREATE");
  g_ns_bind(&g_remote, g_rpmsg_priv, name1, 0x401u);

  CHECK(g_create_count == 1 && g_ack_count == 1,
        "CP bind creates one known-destination endpoint and sends ACK");
  CHECK(g_control.state == BK7258_RPTUN_STATE_CONNECTED,
        "CP bind/ACK marks CONNECTED");
  CHECK((g_control.flags & BK7258_RPTUN_FLAG_CONNECTED_ONCE) != 0,
        "CP bind/ACK preserves CONNECTED_ONCE");
  expect_bootstrap(1, BK7258_RPTUN_NOTIFY_VRING1, 1u);

  g_remote.support_ack = false;
  g_device_created(&g_remote, g_rpmsg_priv);
  CHECK(g_control.state == BK7258_RPTUN_STATE_CONNECTED &&
        g_control.error == 0,
        "late duplicate callback cannot pollute a CONNECTED state");
  g_remote.support_ack = true;

  CHECK(bk7258_rptun_quiesce() == 0, "connected CP RPTUN can quiesce");
  CHECK(g_poweroff_count == 1 && g_destroy_count == 1,
        "poweroff destroys the proof endpoint exactly once");
  CHECK(g_control.state == BK7258_RPTUN_STATE_QUIESCING,
        "lower stop publishes QUIESCING");

  g_control.flags = 0;
  g_control.error = 0;
  CHECK(bk7258_rptun_initialize(2) == 0,
        "persistent CP lower-half restarts at generation two");
  CHECK(g_boot_count == 1, "generation change uses rptun_boot once");
  CHECK(g_register_count == 1,
        "restart reuses the lifetime proof callback registration");
  CHECK(g_control.generation == 2 &&
        g_control.state == BK7258_RPTUN_STATE_CONNECTING,
        "restart publishes generation two CONNECTING");
  expect_bootstrap(2, BK7258_RPTUN_NOTIFY_VRING0, RPTUN_NOTIFY_ALL);

  g_device_created(&g_remote, g_rpmsg_priv);
  CHECK(g_ns_match(&g_remote, g_rpmsg_priv, name2, 0x402u),
        "CP accepts only the restarted generation name");
  g_ns_bind(&g_remote, g_rpmsg_priv, name2, 0x402u);
  CHECK(g_create_count == 2 && g_ack_count == 2,
        "restart creates a fresh endpoint and ACK");
  CHECK(g_control.state == BK7258_RPTUN_STATE_CONNECTED,
        "restarted generation reaches CONNECTED");
  expect_bootstrap(2, BK7258_RPTUN_NOTIFY_VRING1, 1u);
  CHECK(g_unregister_count == 0,
        "proof callback is not transiently unregistered after handshake");
}
#endif

int main(void)
{
#ifdef TEST_BK7258_RPTUN_AP
  test_ap_create_ack_receipt_and_destroy();
  printf("bk7258 RPTUN AP proof: %d checks, %d failures\n",
         g_checks, g_failures);
#else
  test_cp_bind_ack_and_restart();
  printf("bk7258 RPTUN CP proof: %d checks, %d failures\n",
         g_checks, g_failures);
#endif
  return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
