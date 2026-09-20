#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise AP native-lease recovery with the maintained source functions."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]

PREFIX = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define FAR
#define OK 0
#define LOG_WARNING 4
#define IFF_UP 0x0001
#define IFF_RUNNING 0x0040
#define BK7258_WIFI_LINK_CONNECTED 2

struct bk7258_wifi_result_s
{
  int status;
  uint32_t link_state;
  uint32_t ipaddr;
  uint32_t netmask;
  uint32_t router;
};

struct net_driver_s
{
  unsigned int d_flags;
  uint32_t d_ipaddr;
  uint32_t d_netmask;
  uint32_t d_draddr;
};

struct bk7258_wifi_driver_s
{
  struct net_driver_s dev;
  bool registered;
  bool ifup;
};

struct bk7258_wifi_control_dev_s
{
  bool native_link_valid;
  struct bk7258_wifi_result_s native_link;
};

static struct bk7258_wifi_driver_s g_bk7258_wifi;
static struct bk7258_wifi_control_dev_s g_bk7258_wifi_control;
static struct bk7258_wifi_result_s g_current_link;
static int g_apply_calls;
static int g_clear_calls;
static int g_apply_result;
static int g_net_lock_depth;
static int g_warning_calls;

static void net_lock(void)
{
  assert(g_net_lock_depth == 0);
  g_net_lock_depth++;
}

static void net_unlock(void)
{
  assert(g_net_lock_depth == 1);
  g_net_lock_depth--;
}

static int test_syslog(int priority, const char *format, ...)
{
  assert(priority == LOG_WARNING);
  assert(strstr(format, "native lease state drift") != NULL);
  g_warning_calls++;
  return 0;
}

#define syslog test_syslog

static int bk7258_wifi_read_link(struct bk7258_wifi_result_s *result)
{
  *result = g_current_link;
  return OK;
}

static int bk7258_wifi_set_native_lease(
  const struct bk7258_wifi_result_s *result)
{
  g_apply_calls++;
  if (g_apply_result < 0)
    {
      return g_apply_result;
    }

  g_bk7258_wifi.registered = true;
  g_bk7258_wifi.ifup = true;
  g_bk7258_wifi.dev.d_flags = IFF_UP | IFF_RUNNING;
  g_bk7258_wifi.dev.d_ipaddr = result->ipaddr;
  g_bk7258_wifi.dev.d_netmask = result->netmask;
  g_bk7258_wifi.dev.d_draddr = result->router;
  return OK;
}

static int bk7258_wifi_clear_native_lease(void)
{
  g_clear_calls++;
  g_bk7258_wifi.ifup = false;
  g_bk7258_wifi.dev.d_flags &= ~(IFF_UP | IFF_RUNNING);
  g_bk7258_wifi.dev.d_ipaddr = 0;
  g_bk7258_wifi.dev.d_netmask = 0;
  g_bk7258_wifi.dev.d_draddr = 0;
  return OK;
}
"""

TEST = r"""
static void reset_connected(void)
{
  memset(&g_bk7258_wifi, 0, sizeof(g_bk7258_wifi));
  memset(&g_bk7258_wifi_control, 0, sizeof(g_bk7258_wifi_control));
  memset(&g_current_link, 0, sizeof(g_current_link));
  g_current_link.link_state = BK7258_WIFI_LINK_CONNECTED;
  g_current_link.ipaddr = 0x6500a8c0;
  g_current_link.netmask = 0x00ffffff;
  g_current_link.router = 0x0100a8c0;
  g_bk7258_wifi.registered = true;
  g_bk7258_wifi.ifup = true;
  g_bk7258_wifi.dev.d_flags = IFF_UP | IFF_RUNNING;
  g_bk7258_wifi.dev.d_ipaddr = g_current_link.ipaddr;
  g_bk7258_wifi.dev.d_netmask = g_current_link.netmask;
  g_bk7258_wifi.dev.d_draddr = g_current_link.router;
  g_bk7258_wifi_control.native_link_valid = true;
  g_bk7258_wifi_control.native_link = g_current_link;
  g_apply_calls = 0;
  g_clear_calls = 0;
  g_apply_result = OK;
  g_warning_calls = 0;
}

int main(void)
{
  struct bk7258_wifi_result_s result;

  reset_connected();
  assert(!bk7258_wifi_native_lease_matches(NULL));
  assert(bk7258_wifi_native_lease_matches(&g_current_link));
  assert(bk7258_wifi_sync_native_link(&result) == OK);
  assert(g_apply_calls == 0 && g_clear_calls == 0 && g_warning_calls == 0);

  /* Vendor stop retired its private state and carrier, while the CP lease
   * cache still describes the same DHCP lease.
   */

  reset_connected();
  g_bk7258_wifi.ifup = false;
  g_bk7258_wifi.dev.d_flags = IFF_UP;
  assert(!bk7258_wifi_native_lease_matches(&g_current_link));
  assert(bk7258_wifi_sync_native_link(&result) == OK);
  assert(g_apply_calls == 1 && g_clear_calls == 0 && g_warning_calls == 1);
  assert(bk7258_wifi_native_lease_matches(&g_current_link));

  reset_connected();
  g_bk7258_wifi.dev.d_flags &= ~IFF_RUNNING;
  assert(bk7258_wifi_sync_native_link(&result) == OK);
  assert(g_apply_calls == 1 && g_warning_calls == 1);

  reset_connected();
  g_current_link.link_state = 0;
  g_current_link.ipaddr = 0;
  g_current_link.netmask = 0;
  g_current_link.router = 0;
  assert(bk7258_wifi_sync_native_link(&result) == OK);
  assert(g_apply_calls == 0 && g_clear_calls == 1);
  assert(g_bk7258_wifi_control.native_link.link_state == 0);
  assert(bk7258_wifi_sync_native_link(&result) == OK);
  assert(g_clear_calls == 1);

  reset_connected();
  g_bk7258_wifi_control.native_link.ipaddr ^= 1;
  g_apply_result = -EIO;
  assert(bk7258_wifi_sync_native_link(&result) == -EIO);
  assert(g_apply_calls == 1);
  assert(g_bk7258_wifi_control.native_link.ipaddr != g_current_link.ipaddr);
  assert(g_net_lock_depth == 0);
  return 0;
}
"""


class WifiNativeLeaseDriftTest(unittest.TestCase):
    def test_cached_lease_recovers_local_netdev_drift(self):
        ap_source = (ROOT / "chips/bk7258/ap/bk7258_wifi.c").read_text()
        matcher = ap_source[
            ap_source.index("bool bk7258_wifi_native_lease_matches(") : ap_source.index(
                "\n#ifdef CONFIG_NETDB_DNSCLIENT",
                ap_source.index("bool bk7258_wifi_native_lease_matches("),
            )
        ]

        control_source = (
            ROOT / "chips/bk7258/common/bk7258_wifi_control.c"
        ).read_text()
        synchronizer = control_source[
            control_source.index(
                "static int bk7258_wifi_apply_native_lease("
            ) : control_source.index("static int bk7258_wifi_read_status(")
        ]

        with tempfile.TemporaryDirectory() as directory:
            directory_path = Path(directory)
            source_path = directory_path / "test.c"
            binary_path = directory_path / "test"
            source_path.write_text(PREFIX + matcher + synchronizer + TEST)
            subprocess.run(
                [
                    "cc",
                    "-std=gnu11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(source_path),
                    "-o",
                    str(binary_path),
                ],
                check=True,
            )
            subprocess.run([str(binary_path)], check=True)


if __name__ == "__main__":
    unittest.main()
