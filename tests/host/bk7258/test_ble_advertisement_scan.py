#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile the production scan adapter; mock only Host/thread/radio boundaries."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
PREFIX = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>
#define CONFIG_BK7258_AP_CORE 1
#define OK 0
#define SP_UNLOCKED 0
#define BT_LE_SCAN_FILTER_DUP_ENABLE 1
#define BK7258_RADIO_MODE_BLE_SCAN 3
#define cpu_set_t unsigned long
#define pthread_attr_t unsigned long
#define pthread_t unsigned long
typedef unsigned int irqstate_t;
typedef int spinlock_t;
typedef struct {uint8_t type,val[6];} bt_addr_le_t;
static irqstate_t spin_lock_irqsave(spinlock_t *p){(void)p;return 0;}
static void spin_unlock_irqrestore(spinlock_t *p,irqstate_t f){(void)p;(void)f;}
static void *(*queued)(void *);static void *queued_arg;
static int launch_error,lease,start_error,stop_error,update_error,stop_calls,update_calls;
static int pthread_attr_init(pthread_attr_t *a){*a=0;return 0;}
static int pthread_attr_setstacksize(pthread_attr_t *a,size_t s){(void)a;assert(s>=8192);return 0;}
static int pthread_attr_setaffinity_np(pthread_attr_t *a,size_t n,const cpu_set_t *c){(void)a;(void)n;assert(*c==1);return 0;}
static int pthread_attr_destroy(pthread_attr_t *a){(void)a;return 0;}
static int pthread_create(pthread_t *t,const pthread_attr_t *a,void *(*f)(void *),void *arg)
{(void)t;(void)a;if(launch_error)return launch_error;assert(!queued);queued=f;queued_arg=arg;return 0;}
static int pthread_detach(pthread_t t){(void)t;return 0;}
static int bk7258_radio_mode_acquire(int mode){(void)mode;if(lease)return -EBUSY;lease=1;return 0;}
static int bk7258_radio_mode_release(int mode){(void)mode;assert(lease);lease=0;return 0;}
int bt_start_scanning(uint8_t f,void (*cb)(const bt_addr_le_t *,int8_t,uint8_t,const uint8_t *,uint8_t))
{(void)f;(void)cb;return start_error;}
int bt_stop_scanning(void){stop_calls++;return stop_error;}
int bt_le_scan_update(void){update_calls++;return update_error;}
static void run(void){void *(*f)(void *)=queued;void *arg=queued_arg;assert(f);queued=NULL;f(arg);}
"""
TEST = r"""
int main(void)
{
 struct bk7258_ble_scan_snapshot_s s;
 bt_addr_le_t a={0};uint8_t payload[31]={2,9,'x'};
 assert(bk7258_ble_scan_poll(NULL)==-EINVAL);
 launch_error=EAGAIN;assert(bk7258_ble_scan_start()==-EAGAIN && !lease);
 launch_error=0;start_error=-EIO;
 assert(bk7258_ble_scan_start()==0 && lease);run();assert(!lease);
 assert(bk7258_ble_scan_poll(&s)==0 && s.last_error==-EIO);
 start_error=-EALREADY;int stops=stop_calls;
 assert(bk7258_ble_scan_start()==0);run();assert(!lease && stop_calls==stops);
 start_error=0;
 assert(bk7258_ble_scan_start()==0);assert(bk7258_ble_scan_stop()==0);
 run();assert(!lease); /* stop during STARTING must not leak */
 assert(bk7258_ble_scan_start()==0);run();
 scan_report(&a,-20,0,payload,3);scan_report(&a,-25,0,payload,3);
 assert(bk7258_ble_scan_poll(&s)==0 && s.count==1 && s.results[0].rssi==-25);
 for(int i=1;i<17;i++){a.val[0]=i;scan_report(&a,-30,0,payload,3);}
 scan_report(NULL,0,0,NULL,0);scan_report(&a,0,0,payload,32);
 assert(bk7258_ble_scan_poll(&s)==0 && s.count==16 && s.dropped==1);
 stop_error=-EIO;assert(bk7258_ble_scan_stop()==0);run();
 assert(lease && bk7258_ble_scan_start()==-EBUSY);
 assert(bk7258_ble_scan_poll(&s)==0 && s.state==BK7258_BLE_SCAN_FAULTED);
 stop_error=0;assert(bk7258_ble_scan_stop()==0);run();assert(!lease && update_calls==1);
 assert(bk7258_ble_scan_start()==0);run();
 assert(bk7258_ble_scan_poll(&s)==0 && s.count==0);
 assert(bk7258_ble_scan_stop()==0);run();assert(!lease);
 start_error=-EIO;stop_error=-EIO;
 assert(bk7258_ble_scan_start()==0);run();assert(lease);
 update_error=0;assert(bk7258_ble_scan_stop()==0);run();assert(!lease);
 puts("BLE_ADAPTER_HOST_PASS: production lifecycle; mocked HCI, no radio proof");
 return 0;
}
"""


class BleScanTest(unittest.TestCase):
    def test_lifecycle_and_bounded_reports(self):
        source = (ROOT / "chips/bk7258/ap/bk7258_ble_scan.c").read_text()
        source = "\n".join(
            line
            for line in source.splitlines()
            if not line.startswith(("#include", "#if", "#endif"))
        )
        header = ROOT / "chips/bk7258/include/bk7258_ble_scan.h"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(
                PREFIX + f'\n#include "{header}"\n' + source + TEST
            )
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
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
