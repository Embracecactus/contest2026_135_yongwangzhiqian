#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise USB descriptor snapshot caching without an SDK HCD."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]


class UsbhostSnapshotTest(unittest.TestCase):
    def test_generation_survives_private_state_clear_and_stops_at_max(self):
        source = (ROOT / "chips/bk7258/ap/bk7258_usbhost.c").read_text()
        helper = source[
            source.index("static int bk7258_usbhost_next_generation") : source.index(
                "static void bk7258_usbhost_clear_snapshot"
            )
        ]
        helper = helper[helper.index("static int bk7258_usbhost_next_generation") :]
        harness = r"""
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
static uint32_t g_bk7258_usbhost_generation;
"""
        test = r"""
int main(void) {
 uint32_t private_generation = 0;
 assert(bk7258_usbhost_next_generation() == 0);
 assert(g_bk7258_usbhost_generation == 1);
 private_generation = g_bk7258_usbhost_generation;
 memset(&private_generation, 0, sizeof(private_generation));
 assert(g_bk7258_usbhost_generation == 1);
 g_bk7258_usbhost_generation = UINT32_MAX - 1;
 assert(bk7258_usbhost_next_generation() == 0);
 assert(g_bk7258_usbhost_generation == UINT32_MAX);
 assert(bk7258_usbhost_next_generation() == -ENOSPC);
 assert(g_bk7258_usbhost_generation == UINT32_MAX);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(harness + helper + test)
            subprocess.run(
                [
                    "cc",
                    "-std=gnu11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)

    def test_snapshot_returns_eagain_while_init_lock_is_busy(self):
        source = (ROOT / "chips/bk7258/ap/bk7258_usbhost.c").read_text()
        snapshot = source[
            source.index("int bk7258_usbhost_snapshot(") : source.index(
                "static inline FAR struct bk7258_usbhost_s"
            )
        ]
        harness = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define FAR
#define BK7258_USBHOST_DEVICE_DESC_SIZE 18u
#define BK7258_USBHOST_CONFIG_DESC_MAX 256u
typedef int mutex_t;
typedef unsigned int irqstate_t;
typedef int spinlock_t;
static int init_busy;
static int nxmutex_trylock(mutex_t *lock)
{ (void)lock; return init_busy ? -EBUSY : 0; }
static void nxmutex_unlock(mutex_t *lock) { (void)lock; }
static irqstate_t spin_lock_irqsave(spinlock_t *lock)
{ (void)lock; return 0; }
static void spin_unlock_irqrestore(spinlock_t *lock, irqstate_t flags)
{ (void)lock; (void)flags; }
struct usbhost_hubport_s { bool connected; uint8_t speed; };
struct usbhost_roothubport_s { struct usbhost_hubport_s hport; };
enum bk7258_usbhost_enumeration_state_e { IDLE, RUNNING, COMPLETE, FAILED };
struct bk7258_usbhost_snapshot_s {
 bool initialized, connected; uint8_t speed; uint32_t connection_generation;
 enum bk7258_usbhost_enumeration_state_e enumeration_state;
 int32_t enumeration_result; bool device_descriptor_valid;
 uint8_t device_descriptor[BK7258_USBHOST_DEVICE_DESC_SIZE];
 bool configuration_descriptor_valid; uint16_t configuration_length;
 bool configuration_truncated;
 uint8_t configuration_descriptor[BK7258_USBHOST_CONFIG_DESC_MAX];
};
struct bk7258_usbhost_s {
 struct usbhost_roothubport_s rhport; spinlock_t lock; bool initialized;
 bool shutting_down; uint32_t connection_generation;
 enum bk7258_usbhost_enumeration_state_e enumeration_state;
 int enumeration_result; uint16_t configuration_length;
 bool device_descriptor_valid, configuration_descriptor_valid,
      configuration_truncated;
 uint8_t device_descriptor[BK7258_USBHOST_DEVICE_DESC_SIZE];
 uint8_t configuration_descriptor[BK7258_USBHOST_CONFIG_DESC_MAX];
};
static struct bk7258_usbhost_s g_bk7258_usbhost;
static mutex_t g_bk7258_usbhost_init_lock;
"""
        test = r"""
int main(void) {
 struct bk7258_usbhost_snapshot_s out;
 memset(&out, 0xa5, sizeof(out)); init_busy = 1;
 assert(bk7258_usbhost_snapshot(&out) == -EAGAIN);
 assert(((uint8_t *)&out)[0] == 0xa5);
 init_busy = 0; g_bk7258_usbhost.initialized = true;
 g_bk7258_usbhost.rhport.hport.connected = true;
 g_bk7258_usbhost.rhport.hport.speed = 2;
 g_bk7258_usbhost.connection_generation = 9;
 g_bk7258_usbhost.device_descriptor_valid = true;
 g_bk7258_usbhost.device_descriptor[0] = 18;
 assert(bk7258_usbhost_snapshot(&out) == 0);
 assert(out.initialized && out.connected && out.connection_generation == 9);
 assert(out.device_descriptor_valid && out.device_descriptor[0] == 18);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(harness + snapshot + test)
            subprocess.run(
                [
                    "cc",
                    "-std=gnu11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)

    def test_snapshot_lifecycle_contract_is_nonblocking_and_monotonic(self):
        source = (ROOT / "chips/bk7258/ap/bk7258_usbhost.c").read_text()
        self.assertIn("static uint32_t g_bk7258_usbhost_generation;", source)
        self.assertIn("nxmutex_trylock(&g_bk7258_usbhost_init_lock)", source)
        self.assertIn("return -EAGAIN;", source)
        self.assertIn("g_bk7258_usbhost_generation == UINT32_MAX", source)
        self.assertIn("priv->enumeration_result = -ENOSPC;", source)

    def test_descriptor_cache_rejects_short_or_old_and_bounds_config(self):
        source = (ROOT / "chips/bk7258/ap/bk7258_usbhost.c").read_text()
        cache = source[
            source.index("static void bk7258_usbhost_clear_snapshot(") : source.index(
                "static void bk7258_usbhost_enumeration_complete("
            )
        ]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define FAR
#define BK7258_USBHOST_DEVICE_DESC_SIZE 18u
#define BK7258_USBHOST_CONFIG_DESC_MAX 256u
#define BK7258_USBHOST_ENUMERATION_IDLE 0
#define USB_REQ_DIR_IN 0x80
#define USB_REQ_TYPE_STANDARD 0x00
#define USB_REQ_RECIPIENT_DEVICE 0x00
#define USB_REQ_GETDESCRIPTOR 0x06
#define USB_DESC_TYPE_DEVICE 0x01
#define USB_DESC_TYPE_CONFIG 0x02
#define USB_SIZEOF_DEVDESC 18u
#define USB_SIZEOF_CFGDESC 9u
typedef unsigned int irqstate_t;
typedef int spinlock_t;
static irqstate_t spin_lock_irqsave(spinlock_t *lock)
{ (void)lock; return 0; }
static void spin_unlock_irqrestore(spinlock_t *lock, irqstate_t flags)
{ (void)lock; (void)flags; }
struct usbhost_hubport_s { bool connected; };
struct usbhost_roothubport_s { struct usbhost_hubport_s hport; };
struct usb_ctrlreq_s { uint8_t type; uint8_t req; uint8_t value[2];
                       uint8_t index[2]; uint8_t len[2]; };
struct bk7258_usbhost_s {
 spinlock_t lock; struct usbhost_roothubport_s rhport; bool initialized;
 bool shutting_down; uint32_t connection_generation; uint8_t enumeration_state;
 int enumeration_result; uint16_t configuration_length;
 bool device_descriptor_valid, configuration_descriptor_valid,
      configuration_truncated;
 uint8_t device_descriptor[BK7258_USBHOST_DEVICE_DESC_SIZE];
 uint8_t configuration_descriptor[BK7258_USBHOST_CONFIG_DESC_MAX];
};
"""
        test = r"""
int main(void) {
 struct bk7258_usbhost_s priv = { .initialized = true };
 struct usb_ctrlreq_s req = { .type = USB_REQ_DIR_IN,
  .req = USB_REQ_GETDESCRIPTOR, .value = {0, USB_DESC_TYPE_DEVICE},
  .len = {18, 0} };
 uint8_t bytes[300];
 for (unsigned int i = 0; i < sizeof(bytes); i++) bytes[i] = (uint8_t)i;
 bytes[0] = 18; bytes[1] = USB_DESC_TYPE_DEVICE;
 priv.rhport.hport.connected = true; priv.connection_generation = 7;
 bk7258_usbhost_cache_descriptor(&priv, 7, &req, bytes, 17);
 assert(!priv.device_descriptor_valid);
 bk7258_usbhost_cache_descriptor(&priv, 7, &req, bytes, 19);
 assert(!priv.device_descriptor_valid);
 bk7258_usbhost_cache_descriptor(&priv, 7, &req, bytes, 18);
 assert(priv.device_descriptor_valid && !memcmp(priv.device_descriptor, bytes, 18));
 bytes[0] = 0xee;
 bk7258_usbhost_cache_descriptor(&priv, 6, &req, bytes, 18);
 assert(priv.device_descriptor[0] == 18);
 req.value[1] = USB_DESC_TYPE_CONFIG; req.len[0] = 44; req.len[1] = 1;
 bytes[0] = 9; bytes[1] = USB_DESC_TYPE_CONFIG; bytes[2] = 44; bytes[3] = 1;
 bk7258_usbhost_cache_descriptor(&priv, 7, &req, bytes, 300);
 assert(priv.configuration_descriptor_valid && priv.configuration_length == 256);
 assert(priv.configuration_truncated && priv.configuration_descriptor[255] == 255);
 bk7258_usbhost_clear_snapshot(&priv);
 assert(!priv.device_descriptor_valid && !priv.configuration_descriptor_valid);
 assert(priv.configuration_length == 0 && !priv.configuration_truncated);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(harness + cache + test)
            subprocess.run(
                [
                    "cc",
                    "-std=gnu11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
