#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Production CDC TX plus pinned uart_xmitchars; USB completion is external."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_shaniu_usbcdc_rx import ROOT, definition, SERIAL

SOURCE = ROOT / "chips/bk7258/ap/bk7258_usbcdc.c"
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
typedef int mutex_t;
typedef int irqstate_t;
struct uart_dev_s;
struct uart_buffer_s {int head,tail,size;char *buffer;};
struct uart_ops_s {
 void (*send)(struct uart_dev_s *,int);
 ssize_t (*sendbuf)(struct uart_dev_s *,const char *,size_t);
 bool (*txready)(struct uart_dev_s *);
 void (*txint)(struct uart_dev_s *,bool);
};
struct uart_dev_s {void *priv;struct uart_buffer_s xmit;const struct uart_ops_s *ops;};
typedef struct uart_dev_s uart_dev_t;
struct bk7258_usbcdc_config_s {uint8_t ep_bulk_in;};
#define uart_txready(d) ((d)->ops->txready(d))
#define uart_send(d,b) ((d)->ops->send(d,b))
#define uart_sendbuf(d,b,n) ((d)->ops->sendbuf(d,b,n))
#define uart_disabletxint(d) ((d)->ops->txint(d,false))
static int depth,starts,start_error,signals;
static bool pending;
static const uint8_t *inflight;
static uint32_t inflight_size;
static unsigned char wire[2048];
static unsigned int wire_size;
static irqstate_t enter_critical_section(void){return depth++;}
static void leave_critical_section(irqstate_t old){assert(depth==old+1);depth=old;}
static irqstate_t uart_spinlock(struct uart_dev_s *d,bool tx){(void)d;(void)tx;return enter_critical_section();}
static void uart_spinunlock(struct uart_dev_s *d,bool tx,irqstate_t f){(void)d;(void)tx;leave_critical_section(f);}
static void uart_datasent(struct uart_dev_s *d){(void)d;signals++;}
static int usbd_ep_start_write(uint8_t ep,const uint8_t *data,uint32_t n) {
 assert(ep==0x82 && n>0 && n<=256 && !pending);starts++;
 if(start_error)return start_error;
 pending=true;inflight=data;inflight_size=n;return 0;
}
static void bk7258_usbcdc_kick_tx(struct bk7258_usbcdc_priv_s *);
static void bk7258_usbcdc_send(struct uart_dev_s *,int);
static void bk7258_usbcdc_txint(struct uart_dev_s *,bool);
static bool bk7258_usbcdc_txready(struct uart_dev_s *);
void uart_xmitchars(struct uart_dev_s *);
"""
SUFFIX = r"""
static void begin(void) {
 struct bk7258_usbcdc_priv_s *priv=&g_bk7258_usbcdc;
 static const struct uart_ops_s ops={.send=bk7258_usbcdc_send,.txready=bk7258_usbcdc_txready,.txint=bk7258_usbcdc_txint};
 priv->uartdev.ops=&ops;priv->configured=true;priv->config.ep_bulk_in=0x82;
 /* REAL_TX_BINDING */
}
static void enqueue(unsigned int start,unsigned int n) {
 struct uart_buffer_s *u=&g_bk7258_usbcdc.uartdev.xmit;
 for(unsigned int i=0;i<n;i++) {
  int next=(u->head+1)%u->size;assert(next!=u->tail);
  u->buffer[u->head]=(start+i)%251;u->head=next;
 }
}
static void complete(void) {
 assert(pending && wire_size+inflight_size<=sizeof(wire));
 memcpy(wire+wire_size,inflight,inflight_size);wire_size+=inflight_size;
 pending=false;bk7258_usbcdc_ep_in_cb(0x82,inflight_size);
}
static void finish(unsigned int n) {
 unsigned int attempts=0;
 while(pending && attempts++<2048)complete();
 assert(wire_size==n && bk7258_usbcdc_txempty(NULL));
 for(unsigned int i=0;i<n;i++)assert(wire[i]==i%251);
}
"""


class CdcTxTest(unittest.TestCase):
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
        code = (
            PREFIX.replace(
                "static void bk7258_usbcdc_kick_tx",
                "struct bk7258_usbcdc_priv_s;\nstatic void bk7258_usbcdc_kick_tx",
            )
            + constants
            + "\n"
            + structs
        )
        code += "\nstatic struct bk7258_usbcdc_priv_s g_bk7258_usbcdc;\n"
        for name in [
            "ring_used",
            "ring_free",
            "ring_push",
            "ring_pop",
            "kick_tx",
            "ep_in_cb",
            "send",
            "txint",
            "txready",
            "txempty",
        ]:
            code += definition(source, "bk7258_usbcdc_" + name) + "\n"
        code += definition(SERIAL.read_text(), "uart_xmitchars")
        binding = "\n".join(
            re.findall(
                r"^\s*priv->uartdev\.xmit\.(?:size|buffer)\s*=[^;]+;", source, re.M
            )
        )
        assert binding.count(";") == 2
        code += SUFFIX.replace("/* REAL_TX_BINDING */", binding)
        code += "\nint main(void){" + body + "\nassert(!depth);return 0;}\n"
        with tempfile.TemporaryDirectory(prefix="cdc-tx-") as td:
            p = Path(td)
            (p / "case.c").write_text(code)
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
                    str(p / "case.c"),
                    "-o",
                    str(p / "case"),
                ],
                check=True,
            )
            subprocess.run([str(p / "case")], check=True)

    def test_start_failure(self):
        self.run_case(
            """begin();start_error=-EIO;bk7258_usbcdc_send(NULL,0);
        assert(!pending && !bk7258_usbcdc_txempty(NULL));start_error=0;
        bk7258_usbcdc_txint(NULL,true);finish(1);"""
        )

    def test_queued_progress(self):
        self.run_case(
            """begin();for(unsigned int i=0;i<200;i++)bk7258_usbcdc_send(NULL,i);
        assert(pending);finish(200);"""
        )

    def test_upper_start(self):
        self.run_case(
            """begin();enqueue(0,200);bk7258_usbcdc_txint(NULL,true);
        assert(pending);finish(200);"""
        )

    def test_upper_backpressure(self):
        self.run_case(
            """begin();enqueue(0,200);uart_xmitchars(&g_bk7258_usbcdc.uartdev);
        enqueue(200,200);uart_xmitchars(&g_bk7258_usbcdc.uartdev);
        enqueue(400,100);bk7258_usbcdc_txint(NULL,true);finish(500);"""
        )

    def test_wrong_completion(self):
        self.run_case(
            """begin();bk7258_usbcdc_send(NULL,0);
        bk7258_usbcdc_ep_in_cb(0x81,1);assert(!bk7258_usbcdc_txempty(NULL));
        finish(1);"""
        )


if __name__ == "__main__":
    unittest.main()
