#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile the pinned HCI sync functions with a deterministic ownership stub."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT.parent / "nuttx/wireless/bluetooth/bt_hcicore.c"


def extract(source: str, marker: str) -> str:
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated {marker}")


HARNESS = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define FAR
#define OK 0
#define LOG_ERR 0
#define TIMEOUT_MSEC 2500
#define MSEC2TICK(x) (x)
#define NXMUTEX_INITIALIZER {0}
#define wlerr(...) do { (void)sizeof((__VA_ARGS__)); } while (0)
#define wlinfo(...) do { (void)sizeof((__VA_ARGS__)); } while (0)
#define syslog(...) do { (void)sizeof((__VA_ARGS__)); } while (0)
typedef struct { int posted; int destroyed; } sem_t;
typedef struct { int locked; } mutex_t;
struct bt_buf_s;
struct hci_cmd_sync_s { sem_t sem; struct bt_buf_s *response; int result; };
struct hci_data_s { struct hci_cmd_sync_s *sync; uint16_t opcode; };
struct bt_buf_s { struct { struct hci_data_s hci; } u; int refs; int released; };
struct { struct bt_buf_s *sent_cmd; void *tx_queue; } g_btdev;
static mutex_t g_hci_sync_lock = NXMUTEX_INITIALIZER;
static enum { COMPLETE_OK, COMPLETE_STATUS, QUEUE_FAIL, QUEUE_PENDING } mode;
static struct bt_buf_s *event_response;
static int destroyed_posts;
static int nxmutex_lock(mutex_t *m) { assert(!m->locked); m->locked = 1; return 0; }
static int nxmutex_unlock(mutex_t *m) { assert(m->locked); m->locked = 0; return 0; }
static int nxsem_init(sem_t *s, int p, int v) { (void)p; s->posted = v; s->destroyed = 0; return 0; }
static int nxsem_tickwait_uninterruptible(sem_t *s, int t) { (void)t; return s->posted ? 0 : -ETIMEDOUT; }
static void nxsem_post(sem_t *s) { if (s->destroyed) destroyed_posts++; else s->posted++; }
static void nxsem_destroy(sem_t *s) { s->destroyed = 1; }
static struct bt_buf_s *bt_buf_addref(struct bt_buf_s *b) { assert(!b->released); ++b->refs; return b; }
static void bt_buf_release(struct bt_buf_s *b) { assert(b && b->refs > 0); if (!--b->refs) b->released = 1; }
static struct bt_buf_s *bt_hci_cmd_create(uint16_t op, int n) { (void)op; (void)n; return NULL; }
static void hci_cmd_done(uint16_t, uint8_t, struct bt_buf_s *);
static int bt_queue_send(void *queue, struct bt_buf_s *b, int priority) {
  (void)queue; (void)priority;
  if (mode == QUEUE_FAIL) return -EAGAIN;
  nxmutex_lock(&g_hci_sync_lock);
  assert(g_btdev.sent_cmd == NULL);
  g_btdev.sent_cmd = bt_buf_addref(b);
  nxmutex_unlock(&g_hci_sync_lock);
  bt_buf_release(b); /* TX queue releases its handoff reference. */
  if (mode == COMPLETE_OK) hci_cmd_done(b->u.hci.opcode, 0, event_response);
  if (mode == COMPLETE_STATUS) hci_cmd_done(b->u.hci.opcode, 1, event_response);
  return 0;
}
#define BT_NORMAL_PRIO 0
%s
%s
static struct bt_buf_s request(void) { return (struct bt_buf_s){ .u.hci.opcode = 0x200a, .refs = 1 }; }
static struct bt_buf_s response(void) { return (struct bt_buf_s){ .refs = 1 }; }
int main(void) {
  struct bt_buf_s q, r, *out; int ret;
  q=request(); r=response(); event_response=&r; mode=COMPLETE_OK;
  ret=bt_hci_cmd_send_sync(0x200a,&q,NULL); assert(ret == 0 && q.released && r.refs == 1);
  q=request(); r=response(); event_response=&r; mode=COMPLETE_STATUS;
  ret=bt_hci_cmd_send_sync(0x200a,&q,NULL); assert(ret == -EIO && q.released);
  q=request(); r=response(); event_response=&r; mode=COMPLETE_OK; out=NULL;
  ret=bt_hci_cmd_send_sync(0x200a,&q,&out); assert(ret == 0 && out == &r && r.refs == 2); bt_buf_release(out);
  q=request(); r=response(); event_response=&r; mode=QUEUE_FAIL;
  ret=bt_hci_cmd_send_sync(0x200a,&q,NULL); assert(ret == -EAGAIN && q.released);
  q=request(); r=response(); event_response=&r; mode=QUEUE_PENDING;
  ret=bt_hci_cmd_send_sync(0x200a,&q,NULL); assert(ret == -ETIMEDOUT && g_btdev.sent_cmd == &q);
  hci_cmd_done(0x200a,0,&r); assert(destroyed_posts == 0 && q.released);
  puts("hci_sync_lifetime_pass"); return 0;
}
"""


class HciSyncLifetimeTest(unittest.TestCase):
    def test_pinned_source_lifetime_paths(self) -> None:
        if not SOURCE.is_file():
            self.skipTest(f"pinned source missing: {SOURCE}")
        source = SOURCE.read_text(encoding="utf-8")
        for required in (
            "struct hci_cmd_sync_s",
            "g_hci_sync_lock",
            "bt_buf_addref(buf);",
            "buf->u.hci.sync = NULL;",
        ):
            self.assertIn(required, source, f"HCI lifetime patch missing: {required}")
        done = extract(source, "static void hci_cmd_done")
        send = extract(source, "int bt_hci_cmd_send_sync")
        with tempfile.TemporaryDirectory(prefix="bk7258-hci-sync-") as directory:
            fixture = Path(directory) / "fixture.c"
            binary = Path(directory) / "fixture"
            fixture.write_text(HARNESS % (done, send), encoding="utf-8")
            result = subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-value",
                    "-O0",
                    str(fixture),
                    "-o",
                    str(binary),
                ],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run(
                [str(binary)], text=True, capture_output=True, check=False
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "hci_sync_lifetime_pass\n")


if __name__ == "__main__":
    unittest.main()
