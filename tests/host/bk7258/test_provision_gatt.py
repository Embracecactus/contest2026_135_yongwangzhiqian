#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise product GATT queue/window code with deterministic Host callbacks."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PREFIX = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#define CONFIG_BLUETOOTH_MAX_CONN 1
#define SP_UNLOCKED 0
#define BT_UUID_16 16
#define BT_UUID_128 128
#define BT_UUID_GAP 0x1800
#define BT_UUID_GAP_DEVICE_NAME 0x2a00
#define BT_UUID_GAP_APPEARANCE 0x2a01
#define BT_GATT_CCC_NOTIFY 1
#define BT_GATT_CHRC_READ 2
#define BT_GATT_CHRC_WRITE 8
#define BT_GATT_CHRC_NOTIFY 16
#define BT_GATT_PERM_READ 1
#define BT_GATT_PERM_WRITE 2
typedef int spinlock_t;
typedef int irqstate_t;
static int spin_lock_irqsave(spinlock_t *lock) { (void)lock; return 0; }
static void spin_unlock_irqrestore(spinlock_t *lock,int flags) { (void)lock;(void)flags; }
struct bt_conn_s {int refs; bool live;};
struct bt_conn_cb_s {
 void *flink, *context;
 void (*connected)(struct bt_conn_s *,void *);
 void (*disconnected)(struct bt_conn_s *,void *);
};
struct bt_eir_s { uint8_t len, type, data[29]; };
#define BKPROV_GATT_LOCATOR_SIZE 8u
#define GRND_RANDOM 2
#define GRND_NONBLOCK 1
static bool random_missing;
static uint8_t random_sequence;
static ssize_t getrandom(void *out,size_t size,unsigned flags) {
 assert(size==8&&flags==3);
 if(random_missing)return -1;
 memset(out,++random_sequence,size);return size;
}
#define BT_EIR_SVC_DATA128 0x21
#define BT_EIR_FLAGS 1
#define BT_EIR_UUID128_ALL 7
#define BT_EIR_NAME_COMPLETE 9
#define BT_LE_AD_GENERAL 2
#define BT_LE_AD_NO_BREDR 4
#define BT_LE_ADV_IND 0
static struct bt_conn_cb_s *callbacks;
static int starts, stops, disconnects, start_error, stop_error;
static void bt_conn_cb_register(struct bt_conn_cb_s *cb) { callbacks=cb; }
static int bt_start_advertising(uint8_t type,const struct bt_eir_s *ad,
                               const struct bt_eir_s *sd) {
 assert(type==0 && ad[0].len==2 && ad[1].len==17 && ad[2].len==7 && ad[3].len==0);
 assert(ad[1].data[12]==1 && ad[1].data[15]==0x81);
 assert(!memcmp(ad[2].data,"Shaniu",6));
 assert(sd[0].len==(random_missing?0:25));
 if(!random_missing)assert(sd[0].data[16]==random_sequence);
 starts++;return start_error;
}
static int bt_stop_advertising(void) { stops++;return stop_error; }
static int bt_conn_disconnect(struct bt_conn_s *c,uint8_t reason) {
 assert(c && reason==0x13);disconnects++;return 0;
}
struct bt_uuid_s {int type; union {uint16_t u16; uint8_t u128[16];} u;};
struct bt_gatt_attr_s {int handle; const void *data; const void *read; const void *write;};
struct bt_gatt_chrc_s {int properties; int value_handle; struct bt_uuid_s *uuid;};
struct bt_gatt_ccc_cfg_s {int unused;};
#define BT_GATT_PRIMARY_SERVICE(h,u) {h,u,NULL,NULL}
#define BT_GATT_CHARACTERISTIC(h,c) {h,c,NULL,NULL}
#define BT_GATT_DESCRIPTOR(h,u,p,r,w,d) {h,u,r,w}
#define BT_GATT_CCC(h,v,c,f) {h,c,NULL,f}
static int registrations, notifications;
static struct bt_conn_s *notified;
static void bt_gatt_notify(uint16_t h,const void *v,size_t n);
static int bt_gatt_attr_read(struct bt_conn_s *c,const struct bt_gatt_attr_s *a,
 void *out,uint8_t n,uint16_t off,const void *in,uint8_t size) {
 (void)c;(void)a; if(off>size)return -EINVAL; n=n<size-off?n:size-off;
 memcpy(out,(const uint8_t *)in+off,n);return n;
}
static struct bt_conn_s *bt_conn_addref(struct bt_conn_s *c) { assert(c->refs>0);c->refs++;return c; }
static void bt_conn_release(struct bt_conn_s *c) { assert(c->refs>1);c->refs--; }
static void bt_gatt_register(const struct bt_gatt_attr_s *a,size_t n) {
 assert(n==11 && a[0].handle==1 && a[10].handle==0x15);registrations++;
}
"""
TEST = r"""
/* The manifest-pinned NuttX interface is CCC-routed and returns void. */
static void bt_gatt_notify(uint16_t h,const void *v,size_t n) {
 assert(h==0x14 && v && n<=20 && g_peer && g_peer->live);
 notified=g_peer;notifications++;
}
int main(void) {
 struct bt_conn_s a={1,true}, b={1,true};
 uint8_t data[64], out[4096]; memset(data,42,sizeof(data));
 assert(bkprov_gatt_register()==0 && registrations==1);
 assert(bkprov_gatt_register()==-EALREADY);
 ccc_changed(1);
 assert(bkprov_gatt_generation()==0);
 assert(write_tls(&a,NULL,data,64,0)==-EACCES);
 assert(bkprov_gatt_window(true)==0 && starts==1);
 uint8_t connected_hint[8];
 assert(bkprov_gatt_locator(connected_hint)==0);
 assert(bkprov_gatt_window(true)==-EBUSY);
 callbacks->connected(&a,NULL);
 assert(bkprov_gatt_locator(connected_hint)==-EAGAIN);
 assert(connected_hint[0]==0 && !g_locator_valid && g_locator[0]==0);
 assert(bkprov_gatt_poll()==0 && stops==1);
 ccc_changed(1);
 uint32_t first=bkprov_gatt_generation();assert(first!=0);
 for(int i=0;i<64;i++)assert(write_tls(&a,NULL,data,64,0)==64);
 assert(a.refs==3 && write_tls(&a,NULL,data,1,0)==-ENOBUFS);
 assert(bkprov_gatt_read(first+1,out,sizeof(out))==-ESTALE);
 assert(bkprov_gatt_read(first,out,2000)==2000);
 assert(write_tls(&a,NULL,data,64,0)==64);
 assert(bkprov_gatt_read(first,out,sizeof(out))==2160);
 for(int i=0;i<2160;i++)assert(out[i]==42);
 assert(bkprov_gatt_read(first,out,1)==-EAGAIN);
 assert(write_tls(&b,NULL,data,1,0)==-EACCES);
 assert(bkprov_gatt_send(first,data,20)==20 && notified==&a && a.refs==3 && notifications==1);
 assert(bkprov_gatt_send(first,data,20)==20 && a.refs==3 && notifications==2);
 ccc_changed(0);assert(a.refs==2 && bkprov_gatt_generation()==0);
 callbacks->disconnected(&a,NULL);
 assert(a.refs==1 && bkprov_gatt_generation()==0);
 assert(bkprov_gatt_window(true)==0);
 callbacks->connected(&b,NULL);
 ccc_changed(1);uint32_t second=bkprov_gatt_generation();assert(second>first);
 assert(write_tls(&b,NULL,data,1,0)==1);
 assert(bkprov_gatt_send(first,data,1)==-ESTALE);
 assert(bkprov_gatt_send(second,data,1)==1 && notified==&b && notifications==3);
 assert(bkprov_gatt_window(false)==0 && b.refs==2 && disconnects==1);
 callbacks->disconnected(&b,NULL);assert(b.refs==1);
 assert(bkprov_gatt_read(second,out,1)==-ESTALE);
 assert(write_tls(&b,NULL,data,1,0)==-EACCES);
 /* NuttX restores same-peer CCC before product connected(). */
 assert(bkprov_gatt_window(true)==0);
 ccc_changed(1);
 assert(bkprov_gatt_generation()==0);
 callbacks->connected(&a,NULL);
 uint32_t third=bkprov_gatt_generation();assert(third>second);
 assert(write_tls(&a,NULL,data,1,0)==1);
 assert(bkprov_gatt_window(false)==0 && bkprov_gatt_generation()==0);
 callbacks->disconnected(&a,NULL);assert(a.refs==1);
 /* A stale restored CCC cannot re-arm after the window closes. */
 assert(bkprov_gatt_window(true)==0);
 ccc_changed(1);
 assert(bkprov_gatt_generation()==0);
 assert(bkprov_gatt_window(false)==0);
 callbacks->connected(&a,NULL);
 assert(bkprov_gatt_generation()==0);
 assert(write_tls(&a,NULL,data,1,0)==-EACCES);
 callbacks->disconnected(&a,NULL);assert(a.refs==1);
 /* A connected peer without CCC/TLS data still disconnects on close. */
 assert(bkprov_gatt_window(true)==0);
 callbacks->connected(&a,NULL);
 assert(bkprov_gatt_window(false)==0 && disconnects==3);
 callbacks->disconnected(&a,NULL);assert(a.refs==1);
 uint8_t hint[8],old_hint[8];
 assert(bkprov_gatt_locator(hint)==-EAGAIN);
 assert(bkprov_gatt_window(true)==0);
 assert(bkprov_gatt_locator(old_hint)==0);
 assert(bkprov_gatt_window(false)==0);
 assert(bkprov_gatt_locator(hint)==-EAGAIN && hint[0]==0);
 assert(bkprov_gatt_window(true)==0);
 assert(bkprov_gatt_locator(hint)==0 && memcmp(hint,old_hint,8));
 assert(bkprov_gatt_window(false)==0);
 random_missing=true;
 assert(bkprov_gatt_window(true)==0);
 assert(bkprov_gatt_locator(hint)==-EAGAIN);
 assert(bkprov_gatt_window(false)==0);
 random_missing=false;
 /* A failed advertising start does not leave an accepting window. */
 start_error=-EIO;assert(bkprov_gatt_window(true)==-EIO);
 assert(!g_window && !g_advertising);
 start_error=0;assert(bkprov_gatt_window(true)==0);
 stop_error=-ENOBUFS;assert(bkprov_gatt_window(false)==-ENOBUFS);
 assert(!g_window && g_advertising);
 stop_error=-EALREADY;
 assert(bkprov_gatt_poll()==-ENOBUFS && !g_advertising);
 assert(bkprov_gatt_window(true)==-ENOBUFS);
 return 0;
}
"""


class ProductGattTest(unittest.TestCase):
    def test_window_queue_and_connection_generation(self):
        source = (ROOT / "app/bk7258/bk7258_provision_gatt.c").read_text()
        source = re.sub(r"^#include[^\n]*\n", "", source, flags=re.M)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(PREFIX + source + TEST)
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
