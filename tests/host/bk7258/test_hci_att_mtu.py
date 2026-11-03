#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the pinned Host ACL reassembly limit at the negotiated ATT MTU."""

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
NUTTX = Path(os.environ.get("NUTTX_ROOT", ROOT.parent / "nuttx"))
CONN = NUTTX / "wireless/bluetooth/bt_conn.c"
BUF = NUTTX / "wireless/bluetooth/bt_buf.c"
HCI = NUTTX / "wireless/bluetooth/bt_hcicore.c"
WRAPPER = ROOT / "chips/bk7258/ap/bk7258_bt_hci.c"
BLUETOOTH = NUTTX / "include/nuttx/net/bluetooth.h"


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


def extract_define(source: str, name: str) -> str:
    marker = f"#define {name}"
    start = source.index(marker)
    end = source.find("\n", start)
    while source[end - 1] == "\\":
        end = source.find("\n", end + 1)
    return source[start:end]


HARNESS = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FAR
#define wlinfo(...) do {} while (0)
#define wlerr(...) do {} while (0)
#define DEBUGASSERT(x) assert(x)
#define BT_LE162HOST(x) (x)
#define BT_HCI_ACL_NEW 2
#define BT_HCI_ACL_CONTINUATION 1
#define BLUETOOTH_MAX_FRAMELEN 79u

struct iob_s { unsigned char io_data[BLUETOOTH_MAX_FRAMELEN]; };
struct bt_buf_s {
  struct iob_s *frame;
  unsigned char *data;
  uint16_t len;
  int released;
};
struct bt_conn_s { uint16_t rx_len; struct bt_buf_s *rx; };
struct bt_l2cap_hdr_s { uint16_t len; uint16_t cid; } __attribute__((packed));

static int l2cap_calls;
static uint16_t delivered_len;
static unsigned char delivered[BLUETOOTH_MAX_FRAMELEN];

static void bt_buf_release(struct bt_buf_s *buf) { buf->released++; }
static void *bt_buf_extend(struct bt_buf_s *buf, uint16_t length)
{
  void *tail = buf->data + buf->len;
  assert(buf->len + length <= BLUETOOTH_MAX_FRAMELEN);
  buf->len += length;
  return tail;
}
static void bt_l2cap_receive(struct bt_conn_s *conn, struct bt_buf_s *buf)
{
  (void)conn;
  l2cap_calls++;
  delivered_len = buf->len;
  memcpy(delivered, buf->data, buf->len);
  bt_buf_release(buf);
}
%s
%s
%s

static void initialise(struct bt_buf_s *buf, struct iob_s *frame,
                       uint16_t headroom, uint16_t length)
{
  memset(frame, 0, sizeof(*frame));
  memset(buf, 0, sizeof(*buf));
  buf->frame = frame;
  buf->data = frame->io_data + headroom;
  buf->len = length;
}

static void run_case(uint16_t value_length, uint16_t last_fragment,
                     int expect_delivery)
{
  struct bt_conn_s conn = {0};
  struct bt_buf_s first, second, third;
  struct iob_s first_frame, second_frame, third_frame;
  struct bt_l2cap_hdr_s *header;
  uint16_t total = 7u + value_length;
  unsigned int i;

  assert(total == 75u || total == 74u);
  l2cap_calls = 0;
  delivered_len = 0;
  initialise(&first, &first_frame, 5, 27);
  initialise(&second, &second_frame, 5, 27);
  initialise(&third, &third_frame, 5, last_fragment);
  header = (void *)first.data;
  header->len = 3u + value_length;
  header->cid = 4;
  for (i = sizeof(*header); i < first.len; i++) first.data[i] = (unsigned char)i;
  for (i = 0; i < second.len; i++) second.data[i] = (unsigned char)(i + 27u);
  for (i = 0; i < third.len; i++) third.data[i] = (unsigned char)(i + 54u);

  bt_conn_receive(&conn, &first, BT_HCI_ACL_NEW);
  assert(conn.rx == &first && conn.rx_len == total - first.len);
  bt_conn_receive(&conn, &second, BT_HCI_ACL_CONTINUATION);
  if (!expect_delivery)
    {
      assert(conn.rx == &first && conn.rx_len == last_fragment);
      bt_conn_receive(&conn, &third, BT_HCI_ACL_CONTINUATION);
      assert(l2cap_calls == 0 && conn.rx == NULL);
      assert(first.released == 1 && third.released == 1);
      return;
    }

  assert(conn.rx == &first && conn.rx_len == last_fragment);
  bt_conn_receive(&conn, &third, BT_HCI_ACL_CONTINUATION);
  assert(l2cap_calls == 1 && conn.rx == NULL && delivered_len == total);
  assert(first.released == 1 && second.released == 1 && third.released == 1);
}

int main(void)
{
  /* MTU 71 permits a 68-byte value, but 27/27/21 reassembly overruns by one. */
  run_case(68, 21, 0);
  /* Clamping negotiation to MTU 70 makes a 67-byte value fit exactly. */
  run_case(67, 20, 1);
  puts("hci_att_mtu_pass");
  return 0;
}
"""


WRAPPER_HARNESS = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_BK7258_BT_ATT_MTU_COMPAT 1
%s

struct bt_conn_s { int unused; };
struct bt_buf_s { uint8_t *data; uint16_t len; };
struct hci_stats_s {
  uint32_t host_l2cap_rx;
  uint32_t host_mtu_clamped;
  uint32_t last_mtu_clamp;
  uint32_t last_l2cap_rx;
};
struct hci_s { struct hci_stats_s stats; } g_bk7258_bt_hci;
static int forwarded;
static uint8_t forwarded_data[16];
static uint16_t forwarded_len;
static void __real_bt_l2cap_receive(struct bt_conn_s *conn, struct bt_buf_s *buf)
{
  (void)conn;
  forwarded++;
  forwarded_len = buf == NULL ? 0 : buf->len;
  if (buf != NULL) memcpy(forwarded_data, buf->data, buf->len);
}
%s
%s
%s

static uint16_t le16(const uint8_t *data)
{ return (uint16_t)data[0] | (uint16_t)data[1] << 8; }

static void submit(uint16_t l2cap_length, uint16_t cid, uint8_t opcode,
                   uint16_t mtu, uint16_t expected)
{
  struct bt_conn_s conn = {0};
  uint8_t bytes[7] = {0};
  struct bt_buf_s buf = { bytes, sizeof(bytes) };
  bytes[0] = (uint8_t)l2cap_length;
  bytes[1] = (uint8_t)(l2cap_length >> 8);
  bytes[2] = (uint8_t)cid;
  bytes[3] = (uint8_t)(cid >> 8);
  bytes[4] = opcode;
  bytes[5] = (uint8_t)mtu;
  bytes[6] = (uint8_t)(mtu >> 8);
  __wrap_bt_l2cap_receive(&conn, &buf);
  assert(forwarded_len == sizeof(bytes));
  assert(le16(forwarded_data + 5) == expected);
}

int main(void)
{
  submit(3, BK7258_BT_L2CAP_CID_ATT, BK7258_BT_ATT_OP_MTU_REQ, 185, 70);
  submit(3, BK7258_BT_L2CAP_CID_ATT, BK7258_BT_ATT_OP_MTU_REQ, 71, 70);
  assert(g_bk7258_bt_hci.stats.host_mtu_clamped == 2);
  assert(g_bk7258_bt_hci.stats.last_mtu_clamp == ((uint32_t)71 << 16 | 70));
  submit(3, BK7258_BT_L2CAP_CID_ATT, BK7258_BT_ATT_OP_MTU_REQ, 23, 23);
  submit(3, BK7258_BT_L2CAP_CID_ATT, BK7258_BT_ATT_OP_MTU_REQ, 70, 70);
  submit(3, 0x0005, BK7258_BT_ATT_OP_MTU_REQ, 185, 185);
  submit(2, BK7258_BT_L2CAP_CID_ATT, BK7258_BT_ATT_OP_MTU_REQ, 185, 185);
  submit(3, BK7258_BT_L2CAP_CID_ATT, 0x12, 185, 185);
  assert(forwarded == 7 && g_bk7258_bt_hci.stats.host_l2cap_rx == 7);
  puts("hci_att_mtu_wrapper_pass");
  return 0;
}
"""


class HciAttMtuTest(unittest.TestCase):
    def test_pinned_reassembly_boundary(self) -> None:
        for source in (CONN, BUF, HCI):
            self.assertTrue(source.is_file(), f"pinned source missing: {source}")
        conn = CONN.read_text(encoding="utf-8")
        buf = BUF.read_text(encoding="utf-8")
        hci = HCI.read_text(encoding="utf-8")
        self.assertIn("bt_buf_alloc(type, NULL, BLUETOOTH_H4_HDRLEN)", hci)
        self.assertIn("bt_buf_consume(buf, sizeof(*hdr))", hci)
        reset = extract(conn, "static void bt_conn_reset_rx_state")
        receive = extract(conn, "void bt_conn_receive")
        headroom = extract(buf, "size_t bt_buf_headroom")
        tailroom = extract(buf, "size_t bt_buf_tailroom")
        with tempfile.TemporaryDirectory(prefix="bk7258-hci-att-mtu-") as directory:
            fixture = Path(directory) / "fixture.c"
            binary = Path(directory) / "fixture"
            fixture.write_text(
                HARNESS % (headroom, tailroom, reset + "\n" + receive),
                encoding="utf-8",
            )
            compile_result = subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-O0",
                    str(fixture),
                    "-o",
                    str(binary),
                ],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run_result = subprocess.run(
                [str(binary)],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)
            self.assertEqual(run_result.stdout, "hci_att_mtu_pass\n")

    def test_att_mtu_compat_wrapper(self) -> None:
        for source in (WRAPPER, BLUETOOTH):
            self.assertTrue(source.is_file(), f"source missing: {source}")
        wrapper = WRAPPER.read_text(encoding="utf-8")
        bluetooth = BLUETOOTH.read_text(encoding="utf-8")
        self.assertIn("#define BK7258_BT_ATT_RX_MTU", wrapper)
        macros = "\n".join(
            extract_define(bluetooth, name)
            for name in (
                "BLUETOOTH_L2CAP_HDRLEN",
                "BLUETOOTH_ACL_HDRLEN",
                "BLUETOOTH_H4_HDRLEN",
                "BLUETOOTH_MAX_HDRLEN",
                "BLUETOOTH_SMP_MTU",
                "BLUETOOTH_MAX_MTU",
                "BLUETOOTH_MAX_FRAMELEN",
            )
        )
        macros += "\n" + "\n".join(
            extract_define(wrapper, name)
            for name in (
                "BK7258_BT_ACL_HEADER_SIZE",
                "BK7258_BT_L2CAP_HEADER_SIZE",
                "BK7258_BT_L2CAP_CID_ATT",
                "BK7258_BT_ATT_OP_MTU_REQ",
                "BK7258_BT_ATT_MTU_REQ_SIZE",
                "BK7258_BT_ATT_MAX_LE_MTU",
                "BK7258_BT_ATT_RX_MTU",
            )
        )
        get_le16 = extract(wrapper, "static uint16_t bk7258_bt_get_le16")
        put_le16 = extract(wrapper, "static void bk7258_bt_put_le16")
        receive = extract(wrapper, "void __wrap_bt_l2cap_receive")
        with tempfile.TemporaryDirectory(prefix="bk7258-att-mtu-wrapper-") as directory:
            fixture = Path(directory) / "fixture.c"
            binary = Path(directory) / "fixture"
            fixture.write_text(
                WRAPPER_HARNESS % (macros, get_le16, put_le16, receive),
                encoding="utf-8",
            )
            compile_result = subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-O0",
                    str(fixture),
                    "-o",
                    str(binary),
                ],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run_result = subprocess.run(
                [str(binary)],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)
            self.assertEqual(run_result.stdout, "hci_att_mtu_wrapper_pass\n")


if __name__ == "__main__":
    unittest.main()
