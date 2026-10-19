#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise ACL receive ownership through the pinned host receive path."""

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
HCI = ROOT.parent / "nuttx/wireless/bluetooth/bt_hcicore.c"
CONN = ROOT.parent / "nuttx/wireless/bluetooth/bt_conn.c"
WRAPPER = ROOT / "chips/bk7258/ap/bk7258_bt_hci.c"


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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define FAR
#define DEBUGASSERT(x) do { if (!(x)) ownership_error++; } while (0)
#define wlinfo(...) do {} while (0)
#define wlerr(...) do {} while (0)
#define BT_LE162HOST(x) (x)
#define BT_HCI_ACL_NEW 2
#define BT_HCI_ACL_CONTINUATION 1
#define BT_ACL_IN 1
#define BT_EVT 2
#define CONFIG_WIRELESS_BLUETOOTH_HOST 1
#define CONFIG_BK7258_BT_CONN_RX_REF_COMPAT 1
struct bt_buf_s;
struct bt_conn_s { uint16_t handle; uint16_t rx_len; struct bt_buf_s *rx; int refs; };
void __wrap_bt_conn_receive(struct bt_conn_s *conn, struct bt_buf_s *buf,
                            uint8_t flags);
struct bt_hci_acl_hdr_s { uint16_t handle; uint16_t len; } __attribute__((packed));
struct bt_l2cap_hdr_s { uint16_t len; uint16_t cid; } __attribute__((packed));
struct bt_buf_s {
  struct bt_buf_s *flink;
  union { struct { uint16_t handle; } acl; } u;
  unsigned char ref;
  unsigned char type;
  uint16_t len;
  uint16_t capacity;
  unsigned char storage[32];
  unsigned char *data;
  bool released;
};
struct bt_bufferlist_s { struct bt_buf_s *head; };
struct { struct { unsigned host_conn_rx; } stats; } g_bk7258_bt_hci;
static struct bt_conn_s connection;
static struct bt_buf_s spare;
static struct bt_buf_s fallback;
static struct bt_buf_s *queued;
static struct bt_buf_s *inflight;
static int ownership_error;
static int l2cap_calls;
static int reuse_allocations;
static int same_object_reuses;
static int unknown_handle;
static uint16_t bt_acl_handle(uint16_t handle) { return handle & 0x0fff; }
static void bt_buf_consume(struct bt_buf_s *buf, uint16_t length)
{ buf->data += length; buf->len -= length; }
static void *bt_buf_extend(struct bt_buf_s *buf, uint16_t length)
{ void *tail = buf->data + buf->len; buf->len += length; return tail; }
static uint16_t bt_buf_tailroom(struct bt_buf_s *buf) { return buf->capacity - buf->len; }
static struct bt_conn_s *bt_conn_lookup_handle(uint16_t handle)
{ if (unknown_handle || handle != connection.handle) return NULL; connection.refs++; return &connection; }
static void bt_conn_release(struct bt_conn_s *conn) { if (--conn->refs < 0) ownership_error++; }
static void initialise(struct bt_buf_s *buf, unsigned char type)
{ memset(buf, 0, sizeof(*buf)); buf->type = type; buf->ref = 1; buf->capacity = sizeof(buf->storage); buf->data = buf->storage; }
static struct bt_buf_s *pool_alloc(void)
{
  struct bt_buf_s *buf = spare.released || spare.ref == 0 ? &spare :
                         (fallback.released || fallback.ref == 0 ?
                          &fallback : NULL);
  if (buf == NULL) { ownership_error++; return NULL; }
  if (buf == inflight) same_object_reuses++;
  initialise(buf, BT_ACL_IN); queued = buf; return buf;
}
static void bt_buf_release(struct bt_buf_s *buf)
{
  if (buf == NULL || buf->released || buf->ref == 0) { ownership_error++; return; }
  if (--buf->ref == 0) buf->released = true;
}
static struct bt_buf_s * __attribute__((unused)) bt_buf_addref(struct bt_buf_s *buf)
{ if (buf == NULL || buf->released || buf->ref == 0) ownership_error++; else buf->ref++; return buf; }
static void bt_l2cap_receive(struct bt_conn_s *conn, struct bt_buf_s *buf)
{
  (void)conn; l2cap_calls++; bt_buf_release(buf);
  /* A consumer can immediately queue another packet from the same pool. */
  pool_alloc(); reuse_allocations++;
}
static void hci_event(struct bt_buf_s *buf) { (void)buf; ownership_error++; }
static struct bt_buf_s *bt_dequeue_bufwork(struct bt_bufferlist_s *list)
{ struct bt_buf_s *buf = list->head; if (buf != NULL) list->head = buf->flink; return buf; }
%s
%s
%s
%s
%s
static void queue_one(struct bt_bufferlist_s *list, struct bt_buf_s *buf)
{ buf->flink = NULL; list->head = buf; }
static void acl(struct bt_buf_s *buf, uint16_t handle_flags, uint16_t l2len,
                const unsigned char *payload, uint16_t payload_len)
{
  struct bt_hci_acl_hdr_s *acl = (void *)buf->storage;
  struct bt_l2cap_hdr_s *l2 = (void *)(buf->storage + sizeof(*acl));
  acl->handle = handle_flags; acl->len = sizeof(*l2) + payload_len;
  l2->len = l2len; l2->cid = 4;
  memcpy(buf->storage + sizeof(*acl) + sizeof(*l2), payload, payload_len);
  buf->len = sizeof(*acl) + sizeof(*l2) + payload_len;
}
static void acl_cont(struct bt_buf_s *buf, const unsigned char *payload,
                     uint16_t payload_len)
{
  struct bt_hci_acl_hdr_s *acl = (void *)buf->storage;
  acl->handle = 0x1001; acl->len = payload_len;
  memcpy(buf->storage + sizeof(*acl), payload, payload_len);
  buf->len = sizeof(*acl) + payload_len;
}
static void reset_case(void)
{ memset(&connection, 0, sizeof(connection)); connection.handle = 1; connection.refs = 1;
  initialise(&spare, BT_ACL_IN); spare.released = true; spare.ref = 0;
  initialise(&fallback, BT_ACL_IN); fallback.released = true; fallback.ref = 0;
  queued = NULL; inflight = NULL; unknown_handle = 0; }
static void full_packet_reuse_case(void)
{
  struct bt_bufferlist_s list = {0}; const unsigned char p[] = { 1, 2 };
  reset_case(); initialise(&spare, BT_ACL_IN); inflight = &spare;
  acl(&spare, 0x2001, 2, p, sizeof(p)); queue_one(&list, &spare); hci_rx_work(&list);
  if (queued == NULL || queued == &spare || queued->released || queued->ref != 1 ||
      !spare.released || spare.ref != 0 || connection.refs != 1) ownership_error++;
}
static void fragmented_case(void)
{
  struct bt_bufferlist_s list = {0}; struct bt_buf_s first, cont; const unsigned char a[] = { 7 }; const unsigned char b[] = { 8, 9, 10 };
  reset_case(); initialise(&first, BT_ACL_IN); acl(&first, 0x2001, 4, a, sizeof(a)); queue_one(&list, &first); hci_rx_work(&list);
  if (connection.rx != &first || first.released || first.ref != 1 || connection.refs != 1) ownership_error++;
  initialise(&cont, BT_ACL_IN); acl_cont(&cont, b, sizeof(b)); queue_one(&list, &cont); hci_rx_work(&list);
  if (connection.rx != NULL || !first.released || first.ref != 0 ||
      !cont.released || cont.ref != 0 || connection.refs != 1) ownership_error++;
}
static void error_and_unknown_case(void)
{
  struct bt_bufferlist_s list = {0}; struct bt_buf_s cont, unknown; const unsigned char p[] = { 1 };
  reset_case(); initialise(&cont, BT_ACL_IN); acl_cont(&cont, p, sizeof(p)); queue_one(&list, &cont); hci_rx_work(&list);
  if (!cont.released || cont.ref != 0 || connection.refs != 1) ownership_error++;
  reset_case(); initialise(&unknown, BT_ACL_IN); acl(&unknown, 0x2002, 1, p, sizeof(p)); unknown_handle = 1; queue_one(&list, &unknown); hci_rx_work(&list);
  if (!unknown.released || unknown.ref != 0 || connection.refs != 1) ownership_error++;
}
int main(void)
{
  full_packet_reuse_case(); fragmented_case(); error_and_unknown_case();
  if (ownership_error != 0) { fprintf(stderr, "ownership_error=%%d\n", ownership_error); return 1; }
  if (l2cap_calls != 2 || reuse_allocations != 2 || same_object_reuses != 0) return 2;
  puts("hci_acl_lifetime_pass"); return 0;
}
"""


class HciAclLifetimeTest(unittest.TestCase):
    def test_pinned_acl_receive_ownership(self) -> None:
        wrapper_path = Path(os.environ.get("BK7258_HCI_WRAPPER", WRAPPER))
        for source in (HCI, CONN, wrapper_path):
            self.assertTrue(source.is_file(), f"pinned source missing: {source}")
        hci = HCI.read_text(encoding="utf-8")
        conn = CONN.read_text(encoding="utf-8")
        wrapper = wrapper_path.read_text(encoding="utf-8")
        hci_acl = extract(hci, "static void hci_acl")
        hci_work = extract(hci, "static void hci_rx_work")
        reset_rx = extract(conn, "static void bt_conn_reset_rx_state")
        receive = extract(conn, "void bt_conn_receive")
        wrapped = extract(wrapper, "void __wrap_bt_conn_receive")
        hci_acl = hci_acl.replace(
            "bt_conn_receive(conn, buf, flags);",
            "__wrap_bt_conn_receive(conn, buf, flags);",
        )
        receive = receive.replace(
            "void bt_conn_receive(", "void __real_bt_conn_receive("
        )
        with tempfile.TemporaryDirectory(prefix="bk7258-hci-acl-") as directory:
            fixture = Path(directory) / "fixture.c"
            binary = Path(directory) / "fixture"
            fixture.write_text(
                HARNESS % (reset_rx, receive, hci_acl, hci_work, wrapped),
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-function",
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
            self.assertEqual(result.stdout, "hci_acl_lifetime_pass\n")


if __name__ == "__main__":
    unittest.main()
