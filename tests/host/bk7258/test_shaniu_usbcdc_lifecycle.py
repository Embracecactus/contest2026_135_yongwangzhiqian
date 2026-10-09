#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real CDC lifecycle and NuttX uart_connected; scheduler wakeups are peers."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_shaniu_usbcdc_rx import ROOT, PREFIX, definition

SOURCE = ROOT / "chips/bk7258/ap/bk7258_usbcdc.c"
SERIAL = ROOT.parent / "nuttx/drivers/serial/serial.c"


class CdcLifeTest(unittest.TestCase):
    def run_case(self, body):
        source = SOURCE.read_text()
        prefix = PREFIX.replace(
            "struct uart_buffer_s recv;",
            "struct uart_buffer_s recv,xmit;bool disconnected;int fds[1],xmitsem,recvsem;",
        )
        prefix += r"""
#define CONFIG_SERIAL_REMOVABLE 1
#define CONFIG_SERIAL_NPOLLWAITERS 1
#define POLLERR 8
#define POLLHUP 16
#define OK 0
static int polls,wakes;
static void poll_notify(int *fds,int count,int events){(void)fds;assert(count==1 && events==(POLLERR|POLLHUP));polls++;}
static void uart_wakeup(int *sem){(void)sem;wakes++;}
static irqstate_t uart_spinlock(struct uart_dev_s *d,bool tx){(void)d;(void)tx;return enter_critical_section();}
static void uart_spinunlock(struct uart_dev_s *d,bool tx,irqstate_t flags){(void)d;(void)tx;leave_critical_section(flags);}
"""
        constants = "\n".join(
            re.findall(r"^#define BK7258_USBCDC_[RT]XBUFSIZE[^\n]*", source, re.M)
        )
        structs = source[
            source.index("struct bk7258_usbcdc_ring_s\n{") : source.index(
                "static const uint8_t g_bk7258_usbcdc_descriptors"
            )
        ]
        code = prefix + constants + "\n" + structs
        code += "\nstatic struct bk7258_usbcdc_priv_s g_bk7258_usbcdc;\n"
        code += definition(SERIAL.read_text(), "uart_connected")
        for name in [
            "ring_used",
            "ring_free",
            "ring_push",
            "ring_pop",
            "arm_rx",
            "notify",
            "setup",
            "shutdown",
            "txready",
        ]:
            code += definition(source, "bk7258_usbcdc_" + name) + "\n"
        code += (
            "\nint main(void){struct bk7258_usbcdc_priv_s *p=&g_bk7258_usbcdc;p->uartdev.priv=p;p->config.ep_bulk_out=2;p->serial_registered=true;"
            + body
            + "\nassert(!irq_depth);return 0;}\n"
        )
        with tempfile.TemporaryDirectory(prefix="cdc-life-") as td:
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

    def test_wake_disconnect(self):
        self.run_case(
            """bk7258_usbcdc_notify(USBD_EVENT_CONFIGURED,NULL);
        assert(bk7258_usbcdc_setup(&p->uartdev)==0);
        bk7258_usbcdc_notify(USBD_EVENT_DISCONNECTED,NULL);
        assert(p->uartdev.disconnected && polls==1 && wakes==2);"""
        )

    def test_fast_reconnect(self):
        self.run_case(
            """bk7258_usbcdc_notify(USBD_EVENT_CONFIGURED,NULL);
        assert(bk7258_usbcdc_setup(&p->uartdev)==0);endpoint_armed=false;
        bk7258_usbcdc_notify(USBD_EVENT_RESET,NULL);
        bk7258_usbcdc_notify(USBD_EVENT_CONFIGURED,NULL);
        assert(p->uartdev.disconnected && arms==1);
        assert(!bk7258_usbcdc_txready(&p->uartdev));
        bk7258_usbcdc_shutdown(&p->uartdev);assert(!p->uartdev.disconnected);
        assert(bk7258_usbcdc_setup(&p->uartdev)==0);"""
        )

    def test_drop_old_queue(self):
        self.run_case(
            """p->tx.head=3;p->rx.head=4;p->tx_pending=true;
        bk7258_usbcdc_notify(USBD_EVENT_RESET,NULL);
        assert(bk7258_usbcdc_ring_used(&p->tx)==0 && bk7258_usbcdc_ring_used(&p->rx)==0);
        assert(!p->tx_pending);"""
        )

    def test_new_open(self):
        self.run_case(
            """bk7258_usbcdc_notify(USBD_EVENT_CONFIGURED,NULL);
        p->uartdev.recv.head=7;p->uartdev.recv.tail=2;
        p->uartdev.xmit.head=9;p->uartdev.xmit.tail=3;
        assert(bk7258_usbcdc_setup(&p->uartdev)==0);
        assert(p->uartdev.recv.head==p->uartdev.recv.tail);
        assert(p->uartdev.xmit.head==p->uartdev.xmit.tail);"""
        )

    def test_offline_open(self):
        self.run_case(
            "assert(bk7258_usbcdc_setup(&p->uartdev)==-ENOTCONN && !p->opened);"
        )


if __name__ == "__main__":
    unittest.main()
