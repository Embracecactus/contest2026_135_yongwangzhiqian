#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real CDC RX and NuttX uart_recvchars; USB/IRQ and application reads are peers."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "chips/bk7258/ap/bk7258_usbcdc.c"
SERIAL = ROOT.parent / "nuttx/drivers/serial/serial_io.c"


def definition(source, name):
    match = re.search(
        r"(?:static )?(?:inline )?[\w *]+\b" + name + r"\([^;{}]*\)\s*\{", source
    )
    if match is None:
        raise ValueError("missing production function: " + name)
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start() : end]


PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#define FAR
#define LOG_ERR 3
#define syslog(...) ((void)0)
#define USBD_EVENT_CONFIGURED 1
#define USBD_EVENT_RESET 2
#define USBD_EVENT_DISCONNECTED 3
typedef int mutex_t;
typedef int irqstate_t;
struct uart_dev_s;
struct uart_buffer_s {int head,tail,size;char *buffer;};
struct uart_ops_s {
 int (*receive)(struct uart_dev_s *,unsigned int *);
 ssize_t (*recvbuf)(struct uart_dev_s *,char *,size_t);
 bool (*rxavailable)(struct uart_dev_s *);
};
struct uart_dev_s {void *priv;struct uart_buffer_s recv;const struct uart_ops_s *ops;};
typedef struct uart_dev_s uart_dev_t;
#define uart_rxavailable(d) ((d)->ops->rxavailable(d))
#define uart_receive(d,s) ((d)->ops->receive(d,s))
#define uart_recvbuf(d,b,n) ((d)->ops->recvbuf(d,b,n))
static void uart_datareceived(struct uart_dev_s *d){(void)d;}
struct bk7258_usbcdc_config_s {uint8_t ep_bulk_out;};
static int irq_depth, arms, arm_error;
static bool endpoint_armed;
static uint8_t *usb_buffer;
static uint32_t usb_capacity;

static irqstate_t enter_critical_section(void){return irq_depth++;}
static void leave_critical_section(irqstate_t old){assert(irq_depth==old+1);irq_depth=old;}
static int usbd_ep_start_read(uint8_t ep,uint8_t *data,uint32_t size) {
 assert(ep==2 && !endpoint_armed && size==256);arms++;
 if(arm_error)return arm_error;
 endpoint_armed=true;usb_buffer=data;usb_capacity=size;return 0;
}
static int bk7258_usbcdc_receive(struct uart_dev_s *, unsigned int *);
void uart_recvchars(struct uart_dev_s *dev);
static void uart_datasent(struct uart_dev_s *dev){(void)dev;}
"""

SUFFIX = r"""
static void begin(void) {
 struct bk7258_usbcdc_priv_s *priv=&g_bk7258_usbcdc;
 static const struct uart_ops_s ops={.receive=bk7258_usbcdc_receive,.rxavailable=bk7258_usbcdc_rxavailable};
 priv->uartdev.ops=&ops;
 /* REAL_RX_BINDING */
 g_bk7258_usbcdc.config.ep_bulk_out=2;
 bk7258_usbcdc_notify(USBD_EVENT_CONFIGURED,NULL);
}
static void packet(unsigned int start,unsigned int count) {
 assert(endpoint_armed && count<=usb_capacity);endpoint_armed=false;
 for(unsigned int i=0;i<count;i++)usb_buffer[i]=(start+i)%251;
 bk7258_usbcdc_ep_out_cb(2,count);
}
static void consume(unsigned int start,unsigned int count) {
 unsigned int ch;
 for(unsigned int i=0;i<count;i++) {
  assert(bk7258_usbcdc_receive(&g_bk7258_usbcdc.uartdev,&ch)==(int)((start+i)%251));
  assert(ch==0);
 }
}
static void app_read(unsigned int start,unsigned int count) {
 struct uart_buffer_s *r=&g_bk7258_usbcdc.uartdev.recv;
 for(unsigned int i=0;i<count;i++) {
  if(r->head==r->tail)bk7258_usbcdc_rxint(NULL,true);
  assert(r->head!=r->tail);
  assert((unsigned char)r->buffer[r->tail]==(start+i)%251);
  r->tail=(r->tail+1)%r->size;
 }
 bk7258_usbcdc_rxint(NULL,true);
}
"""


class CdcRxTest(unittest.TestCase):
    def run_case(self, body):
        source = SOURCE.read_text()
        constants = "\n".join(
            re.findall(r"^#define BK7258_USBCDC_[RT]XBUFSIZE[^\n]*", source, re.M)
        )
        structs = source[
            source.index("struct bk7258_usbcdc_ring_s\n{") : source.index(
                "static const uint8_t g_bk7258_usbcdc_descriptors"
            )
        ]
        code = PREFIX + constants + "\n" + structs
        header = (ROOT / "chips/bk7258/include/bk7258_usbcdc.h").read_text()
        start = header.index("struct bk7258_usbcdc_snapshot_s\n")
        code += header[start : header.index("};", start) + 2] + "\n"
        code += "\nstatic struct bk7258_usbcdc_priv_s g_bk7258_usbcdc;\n"
        for name in [
            "ring_used",
            "ring_free",
            "ring_push",
            "ring_pop",
            "arm_rx",
            "notify",
            "ep_out_cb",
            "receive",
            "rxint",
            "rxavailable",
        ]:
            code += definition(source, "bk7258_usbcdc_" + name) + "\n"
        code += definition(SERIAL.read_text(), "uart_recvchars")
        code += definition(source, "bk7258_usbcdc_snapshot")
        binding = "\n".join(
            re.findall(
                r"^\s*priv->uartdev\.recv\.(?:size|buffer)\s*=[^;]+;", source, re.M
            )
        )
        assert binding.count(";") == 2
        code += SUFFIX.replace("/* REAL_RX_BINDING */", binding)
        code += "\nint main(void){" + body + r"""
        struct bk7258_usbcdc_snapshot_s snapshot;
        int before = arms;
        uint16_t head = g_bk7258_usbcdc.rx.head, tail = g_bk7258_usbcdc.rx.tail;
        assert(bk7258_usbcdc_snapshot(NULL) == -EINVAL);
        assert(bk7258_usbcdc_snapshot(&snapshot) == 0);
        assert(snapshot.rx_queued == (uint16_t)(head - tail));
        assert(snapshot.rx_bytes == g_bk7258_usbcdc.rx_bytes);
        assert(arms == before && head == g_bk7258_usbcdc.rx.head &&
               tail == g_bk7258_usbcdc.rx.tail);
        assert(!irq_depth);return 0;}
        """
        with tempfile.TemporaryDirectory(prefix="cdc-rx-") as td:
            path = Path(td)
            (path / "case.c").write_text(code)
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-function",
                    "-fsanitize=undefined",
                    "-fno-sanitize-recover=all",
                    str(path / "case.c"),
                    "-o",
                    str(path / "case"),
                ],
                check=True,
            )
            subprocess.run([str(path / "case")], check=True)

    def test_fast_reader(self):
        self.run_case(
            """begin();bk7258_usbcdc_rxint(NULL,true);
        for(unsigned int i=0;i<768;i+=64){packet(i,64);app_read(i,64);}
        assert(endpoint_armed);"""
        )

    def test_slow_reader(self):
        self.run_case(
            """begin();packet(0,256);assert(arms==1 && !endpoint_armed);
        consume(0,255);assert(arms==1);consume(255,1);assert(arms==2 && endpoint_armed);
        packet(256,256);consume(256,256);assert(arms==3);"""
        )

    def test_partial(self):
        self.run_case(
            """begin();packet(0,17);assert(!endpoint_armed);
        consume(0,16);assert(arms==1);consume(16,1);assert(arms==2);"""
        )

    def test_upper_backpressure(self):
        self.run_case(
            """begin();bk7258_usbcdc_rxint(NULL,true);packet(0,256);
        assert(!endpoint_armed);app_read(0,120);assert(endpoint_armed);
        packet(256,256);assert(!endpoint_armed);app_read(120,392);
        assert(endpoint_armed);"""
        )

    def test_arm_failure(self):
        self.run_case(
            """arm_error=-EIO;begin();assert(arms==1 && !endpoint_armed);
        arm_error=0;bk7258_usbcdc_rxint(NULL,true);assert(arms==2 && endpoint_armed);
        packet(0,4);app_read(0,4);"""
        )

    def test_reset(self):
        self.run_case(
            """begin();packet(0,17);endpoint_armed=false;
        bk7258_usbcdc_notify(USBD_EVENT_RESET,NULL);
        unsigned int ch;assert(bk7258_usbcdc_receive(NULL,&ch)==-EAGAIN);
        assert(arms==1);begin();assert(arms==2);packet(90,4);consume(90,4);"""
        )

    def test_duplicate_callback(self):
        self.run_case(
            """begin();packet(0,256);bk7258_usbcdc_ep_out_cb(2,256);
        assert(arms==1);consume(0,256);unsigned int ch;
        assert(bk7258_usbcdc_receive(NULL,&ch)==-EAGAIN);"""
        )


if __name__ == "__main__":
    unittest.main()
