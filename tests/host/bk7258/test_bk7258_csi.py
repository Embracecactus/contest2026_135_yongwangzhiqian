# SPDX-License-Identifier: Apache-2.0
"""Exercise actual CSI capture lifecycle and the C/Python wire boundary."""

import argparse
import importlib.util
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest


ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("csi", ROOT / "tools/bk7258/_lib/csi.py")
csi = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(csi)

PROGRAM = r"""
#include <assert.h>
#include <stdio.h>
#include "bk7258_wifi_csi.c"
static clock_t ticks;
static int link_up = 1, held, stop_error, start_error, send_error;
static int socket_open, start_calls;
static int pm_disabled;
static void (*callback)(struct bk7258_csi_sdk_info_s *);
static unsigned char last_data[272];
static size_t last_size;
clock_t clock_systime_ticks(void) { return ticks; }
int nxsig_usleep(unsigned int us) { ticks += us / 1000; return 0; }
int bk7258_wifi_read_link(struct bk7258_wifi_result_s *result) {
  memset(result, 0, sizeof(*result));
  result->link_state = link_up ? BK7258_WIFI_LINK_CONNECTED : 0;
  return 0;
}
int bk7258_radio_mode_acquire(enum bk7258_radio_mode_e mode) {
  assert(mode == BK7258_RADIO_MODE_WIFI_CSI);
  if (held) return -EBUSY;
  held = 1; return 0;
}
int bk7258_radio_mode_release(enum bk7258_radio_mode_e mode) {
  assert(mode == BK7258_RADIO_MODE_WIFI_CSI && held);
  held = 0; return 0;
}
int psock_socket(int domain, int type, int protocol, struct socket *s) {
  (void)domain;(void)type;(void)protocol;(void)s;
  assert(!socket_open); socket_open = 1;return 0;
}
int psock_close(struct socket *s) {
  (void)s;assert(socket_open);socket_open=0;return 0;
}
int psock_setsockopt(struct socket *s,int level,int option,
  const void *value,socklen_t len) {
  const struct linger *linger=value;
  (void)s;
  assert(level==SOL_SOCKET && option==SO_LINGER && len==sizeof(*linger));
  assert(linger->l_onoff==1 && linger->l_linger==1);
  return 0;
}
ssize_t psock_sendto(struct socket *s, const void *buf, size_t len,
  int flags, const struct sockaddr *peer, socklen_t addrlen) {
  const struct bk7258_wifi_csi_packet_s *p=buf;
  (void)s;(void)peer;(void)addrlen;
  assert(socket_open && flags==MSG_DONTWAIT);
  if(send_error) { send_error=0;return -EAGAIN; }
  if(p->kind==1) { memcpy(last_data,buf,len);last_size=len; }
  return len;
}
void bk_wifi_csi_info_cb_register(void (*cb)(struct bk7258_csi_sdk_info_s *)) {
  callback=cb;
}
int bk_wifi_csi_start_req(uint8_t type,uint8_t mode,uint8_t identity,
  uint8_t format,uint32_t interval,uint32_t delay) {
  assert(pm_disabled);
  assert(type==0 && mode==1 && identity==1 && format==1 && interval==40 && delay==0);
  start_calls++;return start_error;
}
int bk_wifi_csi_stop_req(void) { return stop_error; }
int bk_wifi_sta_pm_disable(void) { pm_disabled=1;return 0; }
int main(void) {
  struct bk7258_wifi_csi_config_s cfg = {
    .address=0x0100007f,.duration_ms=1000,.port=5505,.interval_ms=40,.format=1
  };
  struct bk7258_csi_sdk_info_s info;
  struct bk7258_wifi_csi_result_s result;
  memset(&info,0xa5,sizeof(info));
  info.len=1;info.data_type=0;info.rx.channel=6;info.rx.rssi=-47;
  info.rx.timestamp=123456;info.data.words[0]=0x03ffffff;
  assert(bk7258_wifi_csi_start(NULL)==-EINVAL);
  cfg.interval_ms=9;assert(bk7258_wifi_csi_start(&cfg)==-EINVAL);
  cfg.interval_ms=40;link_up=0;
  assert(bk7258_wifi_csi_start(&cfg)==-ENETDOWN && !held);
  link_up=1;assert(bk7258_wifi_csi_start(&cfg)==0 && held && callback);
  assert(bk7258_wifi_csi_start(&cfg)==-EBUSY);
  callback(NULL);info.len=0;callback(&info);
  info.len=53;callback(&info);info.len=1;info.data_type=1;callback(&info);
  info.data_type=0;
  for (int i=0;i<20;i++) callback(&info);
  bk7258_wifi_csi_snapshot(&result);
  assert(result.callbacks==24 && result.invalid==4 && result.queue_drops==4);
  assert(result.queued==16 && result.sent==0);
  send_error=1;bk7258_wifi_csi_poll();bk7258_wifi_csi_snapshot(&result);
  assert(result.queued==0 && result.sent==15 && result.send_errors==1);
  assert(result.last_error==-EAGAIN);
  assert(last_size==68); /* No unused SDK union bytes escape. */
  assert(fwrite(last_data,1,last_size,stdout)==last_size);
  stop_error=-EIO;
  assert(bk7258_wifi_csi_stop()==-EIO && held && socket_open);
  assert(bk7258_wifi_csi_active()==1);
  callback(&info);bk7258_wifi_csi_snapshot(&result);
  assert(result.callbacks==24); /* Failed stop closes the callback gate. */
  stop_error=0;assert(bk7258_wifi_csi_stop()==0 && !held && !socket_open);
  assert(!callback && bk7258_wifi_csi_active()==0);
  start_error=-EIO;stop_error=-EBUSY;
  assert(bk7258_wifi_csi_start(&cfg)==-EBUSY && held && socket_open);
  start_error=0;stop_error=0;assert(bk7258_wifi_csi_stop()==0 && !held);
  assert(bk7258_wifi_csi_start(&cfg)==0);
  ticks+=1000;bk7258_wifi_csi_poll();
  assert(bk7258_wifi_csi_active()==0 && !held && !socket_open);
  assert(start_calls==3);
  return 0;
}
"""


def packet(kind=1, sent=1, sequence=1, active=1):
    count = 2 if kind == 1 else 0
    return csi.HEADER.pack(
        csi.MAGIC,
        1,
        kind,
        3,
        sequence,
        sequence * 40000,
        sequence,
        0,
        0,
        sent,
        0,
        count,
        0,
        -50,
        6,
        0,
        0,
        active,
        bytes(6),
        0,
        0,
        sequence * 40,
    ) + struct.pack(f"<{count}I", *([0x03FFFFFF] * count))


class CaptureTests(unittest.TestCase):
    def test_actual_capture_lifecycle_and_wire(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            includes = work / "include"
            headers = {
                "nuttx/config.h": "#define CONFIG_BK7258_AP_CORE 1\n#define CONFIG_BK7258_WIFI_CSI 1\n#define CONFIG_BK7258_WIFI_VNET 1\n#define OK 0\n",
                "nuttx/clock.h": "#include <time.h>\n#define MSEC2TICK(v) (v)\n#define TICK2MSEC(v) (v)\nclock_t clock_systime_ticks(void);\n",
                "nuttx/signal.h": "int nxsig_usleep(unsigned int us);\n",
                "nuttx/net/net.h": "#include <sys/socket.h>\nstruct socket {int unused;};\nint psock_socket(int,int,int,struct socket *);\nint psock_close(struct socket *);\nint psock_setsockopt(struct socket *,int,int,const void *,socklen_t);\nssize_t psock_sendto(struct socket *,const void *,size_t,int,const struct sockaddr *,socklen_t);\n",
            }
            for name, value in headers.items():
                path = includes / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(value)
            (includes / "arch/chip").mkdir(parents=True)
            for name in ("bk7258_wifi.h", "bk7258_wifi_csi.h", "bk7258_radio_mode.h"):
                (includes / "arch/chip" / name).symlink_to(
                    ROOT / "chips/bk7258/include" / name
                )
            source = work / "test.c"
            source.write_text(PROGRAM)
            executable = work / "test"
            subprocess.run(
                [
                    "gcc",
                    "-std=gnu11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-O2",
                    "-I",
                    str(includes),
                    "-I",
                    str(ROOT / "chips/bk7258/ap"),
                    str(source),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            raw = subprocess.check_output([str(executable)])
        frame = csi.decode(raw)
        self.assertEqual(frame["count"], 1)
        self.assertEqual(frame["rssi"], -47)
        self.assertEqual(frame["timestamp_us"], 123456)
        self.assertEqual(csi.iq_pairs(frame), [(-1, -1)])
        self.assertEqual(frame["queue_drops"], 4)
        self.assertEqual(frame["sent"], 15)

    def test_bad_lengths_and_types(self):
        raw = packet()
        for bad in (raw[:10], raw[:-1], raw + b"extra", bytes(len(raw))):
            with self.subTest(length=len(bad)), self.assertRaises(ValueError):
                csi.decode(bad)
        self.assertEqual(csi.HEADER.size, 64)

    def test_signed_lanes_and_ruview(self):
        self.assertEqual(
            [csi.signed13(v) for v in (0, 4095, 4096, 8191)], [0, 4095, -4096, -1]
        )
        frame = csi.decode(packet())
        converted = csi.ruview_packet(frame, 58)
        self.assertEqual(len(converted), 24)
        self.assertEqual(struct.unpack_from("<I", converted)[0], 0xC5110001)
        self.assertEqual(struct.unpack_from("<H", converted, 6)[0], 2)
        self.assertEqual(struct.unpack_from("<I", converted, 8)[0], 2437)

    def test_transport_loss_duplicates_and_terminal(self):
        frames = [csi.decode(packet(sent=i, sequence=i)) for i in (1, 3, 3)]
        frames.append(csi.decode(packet(kind=2, sent=4, sequence=4, active=0)))
        for i, frame in enumerate(frames):
            frame.update(source="192.0.2.1", received_ns=i * 40000000)
        result = csi.metrics(frames)["sessions"][0]
        self.assertEqual(result["missing_after_udp_accept"], 2)
        self.assertEqual(result["duplicates"], 1)
        self.assertTrue(result["terminal_observed"])
        self.assertEqual(result["loss_fraction"], 0.5)

    def test_receiver_records_raw_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.bind(("127.0.0.1", 0))
                port = probe.getsockname()[1]
            path = Path(temporary) / "capture.jsonl"
            args = argparse.Namespace(
                bind="127.0.0.1",
                port=port,
                source=None,
                seconds=0.2,
                output=path,
                label="empty",
                ruview=None,
                node_id=58,
            )

            def send():
                time.sleep(0.05)
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as transmitter:
                    transmitter.sendto(packet(), ("127.0.0.1", port))

            thread = threading.Thread(target=send)
            thread.start()
            result = csi.receive(args)
            thread.join()
            self.assertEqual(result["frames"], 1)
            label, frames = csi.read_recording(path)
            self.assertEqual(label, "empty")
            self.assertEqual(frames[0]["words"], [0x03FFFFFF] * 2)
            with self.assertRaises(FileExistsError):
                csi.receive(args)
            self.assertEqual(
                json.loads(path.read_text().splitlines()[0])["label"], "empty"
            )

    def test_calibration_requires_independent_empty_capture(self):
        with self.assertRaises(ValueError):
            csi.calibrate([])
        frames = [csi.decode(packet(sequence=i)) for i in range(1, 41)]
        for frame in frames:
            frame.update(source="192.0.2.1", received_ns=frame["sequence"] * 40000000)
        models = csi.calibrate(frames)
        self.assertEqual(next(iter(models.values()))["training_frames"], 20)
        windows = csi.classify(frames, models, 2)
        self.assertTrue(windows)
        self.assertFalse(any(row["presence_candidate"] for row in windows))


if __name__ == "__main__":
    unittest.main()
