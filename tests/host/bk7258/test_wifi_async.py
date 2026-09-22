#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise actual AP submit/poll/cancel entry points without the SDK worker."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
PREFIX = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define FAR
#define BK7258_WIFI_SSID_MAX_LEN 32
#define BK7258_WIFI_PASSWORD_MAX_LEN 64
#define BK7258_WIFI_PASSWORD_MIN_LEN 8
#define BK7258_WIFI_CONNECT_MIN_MS 5000
#define BK7258_WIFI_CONNECT_MAX_MS 60000
#define BK7258_WIFI_OPERATION_CONNECT 1
#define BK7258_WIFI_OPERATION_PING 4
#define BK7258_WIFI_OPERATION_SCAN 10
#define BK7258_WIFI_SCAN_MIN_MS 1000
#define BK7258_WIFI_CHANNEL_STATS_MAX 13u
#define OK 0
struct bk7258_wifi_monitor_result_s {int status;unsigned channel,frame_count;};
struct bk7258_wifi_channel_stats_s {int status;unsigned returned,dwell_ms;struct bk7258_wifi_monitor_result_s channels[13];};
struct bk7258_wifi_scan_result_s { int status; unsigned returned; };
struct bk7258_wifi_scan_snapshot_s {int status;unsigned found,returned,truncated;unsigned char aps[32];};
struct bk7258_wifi_result_s {int status;uint32_t link_state;};
#define BK7258_WIFI_LINK_CONNECTED 3
struct bk7258_wifi_control_wire_s {int operation;uint32_t timeout_ms;size_t ssid_len,password_len;
 char ssid[33],password[65];};
struct bk7258_wifi_control_dev_s {
 bool initialized,busy,local_pending,local_done,local_cancel,request_local,local_scan,local_channels,local_ping,local_monitor_cleanup;
 struct bk7258_wifi_channel_stats_s local_channel_result;
 struct bk7258_wifi_scan_result_s local_scan_result;
 struct bk7258_wifi_scan_snapshot_s local_scan_snapshot;
 bool trial_active,trial_touched,trial_connected,trial_restore_failed,saved_valid,trial_was_connected;
 uint32_t trial_lease;
 struct bk7258_wifi_control_wire_s saved_connection,trial_connection;
 uint32_t local_ticket;int request_sem;struct bk7258_wifi_control_wire_s request;
 struct bk7258_wifi_result_s local_result;
};
static struct bk7258_wifi_control_dev_s g_bk7258_wifi_control;
static int g_bk7258_wifi_local_lock, post_result, posts;
static int nxmutex_lock(int *p) {(void)p;return 0;}
static void nxmutex_unlock(int *p) {(void)p;}
static int nxsem_post(int *p) {(void)p;posts++;return post_result;}
static int connect_result, current_link, connect_calls;
static char observed_ssid[33];
static int bk7258_wifi_read_link(struct bk7258_wifi_result_s *r) { r->link_state=current_link;return 0; }
static int bk7258_wifi_connect(const struct bk7258_wifi_control_wire_s *q,struct bk7258_wifi_result_s *r) {
 connect_calls++;strcpy(observed_ssid,q->ssid);r->link_state=connect_result?0:3;return connect_result;
}
static int sta_stop_calls, sta_stop_error, hold_link_connected;
static int sync_link_error, operation_order, stop_order, sync_order, monitor_order;
static int bk7258_wifi_stop_sta(void) {
 sta_stop_calls++;stop_order=++operation_order;
 if (sta_stop_error)return sta_stop_error;
 if (!hold_link_connected)current_link=0;
 return 0;
}
static int bk7258_wifi_sync_native_link(struct bk7258_wifi_result_s *r) {
 sync_order=++operation_order;
 if (sync_link_error)return sync_link_error;
 r->link_state=current_link;return 0;
}
static struct {unsigned active;} g_bk7258_wifi_monitor;
static int monitor_starts,monitor_stops,monitor_stop_error,cancel_dwell;
static int bk7258_wifi_monitor_start_capture(unsigned channel) {(void)channel;monitor_order=++operation_order;monitor_starts++;return 0;}
static int bk7258_wifi_monitor_stop_capture(void) {monitor_stops++;return monitor_stop_error;}
static void bk7258_wifi_monitor_snapshot(struct bk7258_wifi_monitor_result_s *r) {r->frame_count=3;}
static void nxsig_usleep(unsigned t) {(void)t;if(cancel_dwell)g_bk7258_wifi_control.local_cancel=true;}
"""
TEST = r"""
static void finish_worker(void) {
 struct bk7258_wifi_control_dev_s *p=&g_bk7258_wifi_control;
 p->local_result.status=bk7258_wifi_trial_run(p,&p->request,&p->local_result);
 p->busy=p->trial_active;p->local_done=true;
}
int main(void) {
 struct bk7258_wifi_control_dev_s *p=&g_bk7258_wifi_control;
 struct bk7258_wifi_result_s result;
 char password[]="test-only-pass";uint32_t first, second;
 assert(bk7258_wifi_connect_async("lab",password,5000,&first)==-EAGAIN);
 p->initialized=true;
 assert(bk7258_wifi_connect_async("lab",password,4999,&first)==-EINVAL);
 struct bk7258_wifi_channel_stats_s channels;
 assert(bk7258_wifi_channels_async(99,&first)==-EINVAL);
 assert(bk7258_wifi_channels_async(300,&first)==0);
 assert(bk7258_wifi_channels_poll(first,&channels)==-EAGAIN);
 assert(bk7258_wifi_channels_run(300,false)==0);
 assert(monitor_starts==13 && monitor_stops==13);
 p->local_done=true;p->busy=false;
 assert(bk7258_wifi_channels_poll(first,&channels)==0 && channels.returned==13);
 assert(bk7258_wifi_channels_async(300,&first)==0);
 cancel_dwell=1;
 assert(bk7258_wifi_channels_run(300,false)==-ECANCELED);
 assert(monitor_starts==14 && monitor_stops==14);
 p->local_done=true;p->busy=false;
 assert(bk7258_wifi_channels_poll(first,&channels)==0);
 cancel_dwell=0;current_link=3;
 assert(bk7258_wifi_channels_run(300,false)==-EBUSY);
 p->local_cancel=true;sta_stop_calls=0;
 assert(bk7258_wifi_channels_run(300,true)==-ECANCELED && sta_stop_calls==0);
 p->local_cancel=false;p->saved_valid=true;
 assert(bk7258_wifi_channels_run(300,true)==0 && sta_stop_calls==1 &&
        !p->saved_valid && current_link==0);
 current_link=3;p->saved_valid=true;sta_stop_error=-EIO;
 { int before=monitor_starts;
   assert(bk7258_wifi_channels_run(300,true)==-EIO && monitor_starts==before &&
          p->saved_valid); }
 sta_stop_error=0;hold_link_connected=1;sta_stop_calls=0;current_link=3;
 { int before=monitor_starts;
   assert(bk7258_wifi_channels_run(300,true)==-ETIMEDOUT &&
          monitor_starts==before); }
 hold_link_connected=0;sync_link_error=-EIO;current_link=3;sta_stop_calls=0;
 p->saved_valid=true;
 { int before=monitor_starts;
   assert(bk7258_wifi_channels_run(300,true)==-EIO && monitor_starts==before &&
          !p->saved_valid); }
 sync_link_error=0;current_link=3;operation_order=0;stop_order=sync_order=monitor_order=0;
 assert(bk7258_wifi_channels_run(300,true)==0 && stop_order < sync_order &&
        sync_order < monitor_order);
 current_link=0;posts=0;
 p->local_cancel=false;monitor_stop_error=-EIO;
 assert(bk7258_wifi_channels_run(300,false)==-EIO && p->local_monitor_cleanup);
 monitor_stop_error=0;
 assert(bk7258_wifi_channels_run(300,false)==0 && !p->local_monitor_cleanup);
 struct bk7258_wifi_scan_result_s scan;
 assert(bk7258_wifi_scan_async(0,&first)==-EINVAL);
 assert(bk7258_wifi_scan_async(15000,&first)==0);
 assert(p->busy && p->local_scan && posts==1);
 assert(bk7258_wifi_scan_poll(first,&scan)==-EAGAIN);
 assert(bk7258_wifi_connect_poll(first,&result)==-ESTALE);
 assert(bk7258_wifi_connect_async("lab",password,5000,&second)==-EBUSY);
 p->local_scan_result.returned=3;p->local_done=true;p->busy=false;
 assert(bk7258_wifi_scan_poll(first+1,&scan)==-ESTALE);
 assert(bk7258_wifi_scan_poll(first,&scan)==0 && scan.returned==3);
  assert(bk7258_wifi_scan_poll(first,&scan)==-ESTALE);
 assert(bk7258_wifi_scan_async(15000,&first)==0);
 p->local_scan_snapshot.found=33;p->local_scan_snapshot.returned=32;
 p->local_scan_snapshot.truncated=1;p->local_done=true;p->busy=false;
 struct bk7258_wifi_scan_snapshot_s snapshot;
 assert(bk7258_wifi_scan_snapshot_poll(first,&snapshot)==0 &&
        snapshot.returned==32 && snapshot.truncated==1);
 assert(bk7258_wifi_scan_poll(first,&scan)==-ESTALE);
 assert(bk7258_wifi_ping_async(999,&first)==-EINVAL);
 assert(bk7258_wifi_ping_async(3000,&first)==0 &&
        p->local_ping && !p->local_scan &&
        p->request.operation==BK7258_WIFI_OPERATION_PING);
 assert(bk7258_wifi_connect_poll(first,&result)==-ESTALE);
 assert(bk7258_wifi_ping_poll(first,&result)==-EAGAIN);
 assert(bk7258_wifi_ping_cancel(first)==0);
 p->local_result.status=-ECANCELED;p->local_done=true;p->busy=false;
 assert(bk7258_wifi_ping_poll(first,&result)==0 && result.status==-ECANCELED);
 assert(bk7258_wifi_ping_poll(first,&result)==-ESTALE);
 post_result=-EIO;
 assert(bk7258_wifi_scan_async(15000,&first)==-EIO);
 assert(!p->busy && !p->local_pending);
 post_result=0;posts=0;
 p->busy=true; // The existing CP request currently owns the same worker.
 assert(bk7258_wifi_connect_async("lab",password,5000,&first)==-EBUSY);
 p->busy=false;
 assert(bk7258_wifi_connect_async("lab",password,5000,&first)==0);
 assert(posts==1 && first!=0 && p->busy && p->request_local);
 memset(password,0,sizeof(password));
 assert(strcmp(p->request.password,"test-only-pass")==0);
 assert(bk7258_wifi_connect_async("other","",5000,&second)==-EBUSY);
 assert(bk7258_wifi_connect_poll(first+1,&result)==-ESTALE);
 assert(bk7258_wifi_connect_poll(first,&result)==-EAGAIN);
 assert(bk7258_wifi_connect_cancel(first+1)==-ESTALE && !p->local_cancel);
 assert(bk7258_wifi_connect_cancel(first)==0 && bk7258_wifi_local_cancelled());
 p->request_local=false;assert(!bk7258_wifi_local_cancelled());
 // Publish a worker completion; stale clients must not consume it.
 p->local_result.status=-ECANCELED;p->local_done=true;p->busy=false;
 assert(bk7258_wifi_connect_cancel(first)==-EALREADY);
 assert(bk7258_wifi_connect_poll(first+1,&result)==-ESTALE);
 assert(bk7258_wifi_connect_poll(first,&result)==0 && result.status==-ECANCELED);
 assert(!p->local_pending && p->local_result.status==0);
 assert(bk7258_wifi_connect_poll(first,&result)==-ESTALE);
 post_result=-EIO;
 assert(bk7258_wifi_connect_async("next","",5000,&second)==-EIO);
 assert(!p->busy && !p->local_pending);
 for(size_t i=0;i<sizeof(p->request);i++)assert(((unsigned char *)&p->request)[i]==0);
 post_result=0;
 assert(bk7258_wifi_connect_async("next","",5000,&second)==0 && second>first);
 assert(bk7258_wifi_connect_poll(first,&result)==-ESTALE);
 p->local_pending=false;p->busy=false;p->local_ticket=UINT32_MAX;
 assert(bk7258_wifi_connect_async("next","",5000,&second)==-EOVERFLOW);
 p->local_ticket=0;
 uint32_t lease, done;
 assert(bk7258_wifi_trial_start("original","",5000,&lease)==0);
 assert(p->trial_active && p->busy);
 assert(bk7258_wifi_trial_finish(lease,true,&done)==-EBUSY);
 finish_worker();assert(p->busy);
 assert(bk7258_wifi_connect_poll(lease,&result)==0 && result.status==0);
 assert(bk7258_wifi_connect_async("interfere","",5000,&second)==-EBUSY);
 assert(bk7258_wifi_trial_finish(lease+1,true,&done)==-ESTALE);
 assert(bk7258_wifi_trial_finish(lease,true,&done)==0);
 finish_worker();assert(!p->busy && p->saved_valid);
 assert(bk7258_wifi_connect_poll(done,&result)==0 && result.status==0);
 assert(!strcmp(p->saved_connection.ssid,"original"));

 current_link=3;connect_result=-EACCES;
 assert(bk7258_wifi_trial_start("wrong-password","",5000,&lease)==0);
 finish_worker();assert(p->busy && !p->trial_connected);
 assert(bk7258_wifi_connect_poll(lease,&result)==0 && result.status==-EACCES);
 assert(bk7258_wifi_trial_finish(lease,true,&done)==-EPERM);
 assert(bk7258_wifi_trial_finish(lease,false,&done)==0);
 connect_result=-EIO;finish_worker();
 assert(!strcmp(observed_ssid,"original") && p->busy && p->trial_restore_failed);
 assert(bk7258_wifi_connect_poll(done,&result)==0 && result.status==-EIO);
 assert(bk7258_wifi_trial_finish(lease,true,&done)==-EPERM);
 assert(bk7258_wifi_trial_finish(lease,false,&done)==0);
 connect_result=0;finish_worker();
 assert(bk7258_wifi_connect_poll(done,&result)==0 && result.status==0 && !p->busy);

 p->saved_valid=false;int previous_calls=connect_calls;
 assert(bk7258_wifi_trial_start("unknown-old-network","",5000,&lease)==0);
 finish_worker();assert(connect_calls==previous_calls);
 assert(bk7258_wifi_connect_poll(lease,&result)==0 && result.status==-ENOKEY);
 assert(bk7258_wifi_trial_finish(lease,false,&done)==0);
 finish_worker();assert(bk7258_wifi_connect_poll(done,&result)==0 && !p->busy && current_link==3);
 return 0;
}
"""


class WifiAsyncTest(unittest.TestCase):
    def test_connection_config_has_no_hidden_flash_or_unbounded_reconnect(self):
        source = (ROOT / "chips/bk7258/common/bk7258_wifi_control.c").read_text()
        config_type = source[source.index("struct bk7258_wifi_sta_config_s\n{") :]
        config_type = config_type[: config_type.index("\n};") + 4]
        setup = source[source.index("  memset(&config, 0, sizeof(config));") :]
        setup = setup[: setup.index("  /* The official v3.1.1.9 API")]
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define FAR
#define BK7258_WIFI_SECURITY_AUTO 12
struct request_s {const char *ssid,*password;unsigned ssid_len,password_len,timeout_ms;};
"""
        test = (
            r"""
int main(void) {
 struct request_s input={"test-only","test-only-pass",9,14,5001};
 struct request_s *request=&input;
 struct bk7258_wifi_sta_config_s config;
"""
            + setup
            + r"""
 assert(!strcmp(config.ssid,"test-only"));
 assert(!strcmp(config.password,"test-only-pass"));
 assert(config.no_auto_fci==1 && config.user_fast_connect==0);
 assert(config.auto_reconnect_count==1 && config.auto_reconnect_timeout==0);
 assert(config.disable_auto_reconnect);
 assert(config.security==BK7258_WIFI_SECURITY_AUTO);
 for(unsigned i=0;i<sizeof(config.reserved);i++)assert(config.reserved[i]==0);
 return 0;
}
"""
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(harness + config_type + test)
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

    def test_scan_security_names_cover_vendor_values_and_unknowns(self):
        source = (ROOT / "chips/bk7258/common/bk7258_wifi_control.c").read_text()
        names = source[
            source.index("const char *bk7258_wifi_security_name(") : source.index(
                "static bool bk7258_wifi_password_length_valid"
            )
        ]
        harness = r"""
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#define BK7258_WIFI_SECURITY_NONE 0
#define BK7258_WIFI_SECURITY_WEP 1
#define BK7258_WIFI_SECURITY_WPA_TKIP 2
#define BK7258_WIFI_SECURITY_WPA_AES 3
#define BK7258_WIFI_SECURITY_WPA_MIXED 4
#define BK7258_WIFI_SECURITY_WPA2_TKIP 5
#define BK7258_WIFI_SECURITY_WPA2_AES 6
#define BK7258_WIFI_SECURITY_WPA2_MIXED 7
#define BK7258_WIFI_SECURITY_WPA3_SAE 8
#define BK7258_WIFI_SECURITY_WPA3_WPA2_MIXED 9
#define BK7258_WIFI_SECURITY_EAP 10
#define BK7258_WIFI_SECURITY_OWE 11
#define BK7258_WIFI_SECURITY_AUTO 12
#define BK7258_WIFI_SECURITY_WAPI_PSK 13
#define BK7258_WIFI_SECURITY_WAPI_CERT 14
#define BK7258_WIFI_SECURITY_WAPI_UNKNOWN 15
"""
        test = r"""
int main(void) {
 assert(!strcmp(bk7258_wifi_security_name(0), "Open"));
 assert(!strcmp(bk7258_wifi_security_name(6), "WPA2-AES"));
 assert(!strcmp(bk7258_wifi_security_name(8), "WPA3-SAE"));
 assert(!strcmp(bk7258_wifi_security_name(11), "OWE"));
 assert(!strcmp(bk7258_wifi_security_name(15), "WAPI-Unknown"));
 assert(bk7258_wifi_security_name(16) == NULL);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(harness + names + test)
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

    def test_scan_selection_is_capacity_parameterized(self):
        source = (ROOT / "chips/bk7258/common/bk7258_wifi_control.c").read_text()
        selection = source[
            source.index("static void bk7258_wifi_scan_copy_ap(") : source.index(
                "static int bk7258_wifi_scan_cleanup(void)"
            )
        ]
        harness = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define FAR
#define BK7258_WIFI_SSID_MAX_LEN 32u
#define BK7258_WIFI_SCAN_MAX_RESULTS 4u
struct bk7258_wifi_scan_ap_s {char ssid[33];uint8_t bssid[6];int32_t rssi;
 uint8_t channel,security,reserved[2];};
struct bk7258_wifi_scan_result_s {int32_t status;uint32_t found,returned,truncated;
 struct bk7258_wifi_scan_ap_s aps[4];};
struct bk7258_wifi_scan_snapshot_s {int32_t status;uint32_t found,returned,truncated;
 struct bk7258_wifi_scan_ap_s aps[32];};
struct bk7258_wifi_scan_ap_sdk_s {char ssid[33];uint8_t bssid[6];int32_t rssi;
 uint8_t channel,security,reserved[2];};
struct bk7258_wifi_scan_result_sdk_s {int ap_num;struct bk7258_wifi_scan_ap_sdk_s *aps;};
"""
        test = r"""
int main(void) {
 struct bk7258_wifi_scan_ap_sdk_s source[33];
 struct bk7258_wifi_scan_ap_s four[4], thirtytwo[32];
 struct bk7258_wifi_scan_snapshot_s snapshot;
 struct bk7258_wifi_scan_result_s legacy;
 uint32_t found,returned,truncated;
 for (unsigned i=0;i<33;i++) { source[i].rssi=-(int32_t)i; source[i].channel=i; }
 struct bk7258_wifi_scan_result_sdk_s sdk={33,source};
 memset(four,0,sizeof(four)); found=returned=truncated=0;
 bk7258_wifi_scan_select_strongest(four,4,&found,&returned,&truncated,&sdk);
 assert(found==33 && returned==4 && truncated==29 && four[0].rssi==0 && four[3].rssi==-3);
 memset(thirtytwo,0,sizeof(thirtytwo)); found=returned=truncated=0;
 bk7258_wifi_scan_select_strongest(thirtytwo,32,&found,&returned,&truncated,&sdk);
 assert(found==33 && returned==32 && truncated==1 && thirtytwo[0].rssi==0 && thirtytwo[31].rssi==-31);
 memset(&snapshot,0,sizeof(snapshot)); snapshot.found=found; snapshot.returned=returned;
 snapshot.truncated=truncated; memcpy(snapshot.aps,thirtytwo,sizeof(thirtytwo));
 bk7258_wifi_scan_result_from_snapshot(&legacy,&snapshot);
 assert(legacy.found==33 && legacy.returned==4 && legacy.truncated==29 && legacy.aps[3].rssi==-3);
 sdk.aps=NULL; found=returned=truncated=0;
 bk7258_wifi_scan_select_strongest(thirtytwo,32,&found,&returned,&truncated,&sdk);
 assert(found==33 && returned==0 && truncated==33);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(harness + selection + test)
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

    def test_scan_cleanup_retains_lease_on_stop_or_unregister_failure(self):
        source = (ROOT / "chips/bk7258/common/bk7258_wifi_control.c").read_text()
        cleanup = source[
            source.index("static int bk7258_wifi_scan_cleanup(void)") : source.index(
                "static int bk7258_wifi_scan(uint32_t"
            )
        ]
        harness = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#define OK 0
#define BK7258_WIFI_EVENT_MOD_WIFI 1
#define BK7258_WIFI_EVENT_SCAN_DONE 2
#define BK7258_RADIO_MODE_WIFI_SCAN 3
struct bk7258_wifi_control_dev_s {
 bool scan_cleanup_pending,scan_started,scan_callback_registered;
};
static struct bk7258_wifi_control_dev_s g_bk7258_wifi_control;
static int stop_error,unregister_error,releases,stops,unregisters;
static int bk7258_wifi_vendor_result(int ret){return ret;}
static int bk_wifi_scan_stop(void){stops++;return stop_error;}
static void bk7258_wifi_scan_event(void){}
static int bk_event_unregister_cb(int m,int e,void (*f)(void))
{(void)m;(void)e;(void)f;unregisters++;return unregister_error;}
static int bk7258_radio_mode_release(int m){(void)m;releases++;return 0;}
"""
        test = r"""
int main(void) {
 struct bk7258_wifi_control_dev_s *p=&g_bk7258_wifi_control;
 p->scan_cleanup_pending=p->scan_started=p->scan_callback_registered=true;
 stop_error=-EIO;
 assert(bk7258_wifi_scan_cleanup()==-EIO && p->scan_cleanup_pending);
 assert(releases==0 && unregisters==0);
 stop_error=0;unregister_error=-EIO;
 assert(bk7258_wifi_scan_cleanup()==-EIO && p->scan_cleanup_pending);
 assert(!p->scan_started && p->scan_callback_registered && releases==0);
 int old_stops=stops;unregister_error=0;
 assert(bk7258_wifi_scan_cleanup()==0 && releases==1);
 assert(!p->scan_cleanup_pending && !p->scan_callback_registered && stops==old_stops);
 assert(bk7258_wifi_scan_cleanup()==0 && releases==1);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(harness + cleanup + test)
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

    def test_owner_tickets_cancellation_and_failed_enqueue(self):
        source = (ROOT / "chips/bk7258/common/bk7258_wifi_control.c").read_text()
        helper = source[source.index("static bool bk7258_wifi_password_length_valid") :]
        helper = helper[: helper.index("\n}\n") + 3]
        functions = source[
            source.index("/* Private worker operations:") : source.index(
                "static int bk7258_wifi_connect("
            )
        ]
        functions = functions.replace('"dmb sy"', '""')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            trial = source[
                source.index("static int bk7258_wifi_trial_run(") : source.index(
                    "static void bk7258_wifi_control_report_immediate("
                )
            ]
            (path / "test.c").write_text(PREFIX + helper + functions + trial + TEST)
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
