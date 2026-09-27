#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real product reset coordinator; external participants are deterministic peers."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_nfc_rf_lifecycle import function, ROOT

PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <stdatomic.h>
#include "bk7258_pc_tasks.h"
static struct bkpc_tasks_s g_pc_tasks;
#define CONFIG_BK7258_USBCDC 1
static int usb_error, usb_stops;
static bool usb_closed;
static int product_pc_usb_stop(void) { usb_stops++;usb_closed=!usb_error;return usb_error; }
#define CONFIG_BK7258_NFC_SERVICE 1
#define CONFIG_BK7258_MOTION_SERVICE 1
static int motion_error, motion_resume_error, motion_stops, motion_resumes;
static bool motion_closed;
static int bk7258_motion_service_quiesce(bool stop) {
 if(stop){motion_stops++;motion_closed=true;return motion_error;}
 motion_resumes++;if(motion_resume_error)return motion_resume_error;motion_closed=false;return 0;
}
#define CONFIG_BK7258_PRODUCT_KEYS 1
#define CONFIG_BK7258_PM_SOFT_OFF 1
enum { PRODUCT_RESET_IDLE, PRODUCT_RESET_QUIESCING, PRODUCT_RESET_FINISHING };
static int g_reset_phase, pending=1, nfc_error, resume_error, owner_error;
static int stops, resumes, finishes, clears, owner_opens, g_identity, owner_resume_error;
static int g_config_revision;
static bool owner_closed=true;
static bool nfc_closed, g_shutdown_requested, g_shutdown_failed, g_power_pending;
static bool g_identity_bound=true, g_control_bound=true, g_configured=true, g_cloud_loaded=true;
static atomic_bool g_voice_initialized, g_trigger_prepare_pending;
static atomic_int g_active_persona;
static int bkprov_storage_reset_pending(void) { return pending; }
static int bk7258_nfc_service_quiesce(bool stop) {
 if(stop){stops++;nfc_closed=true;return nfc_error;}
 resumes++;if(resume_error)return resume_error;nfc_closed=false;return 0;
}
static int bkprov_owner_quiesce(bool stop) {
 if(!stop){owner_closed=false;owner_opens++;return owner_resume_error;}
 owner_closed=true;return owner_error;
}
static int bkprov_network_cancel(void){return 0;}
static void bkprov_network_step(void){}
static bool bkprov_network_busy(void){return false;}
static bool voice_channel_is_idle(void){return true;}
static void voice_channel_cancel(void){}
static int voice_channel_recover(void){return 0;}
static int product_clear(void *p){(void)p;clears++;return 0;}
static int product_reset_cleanup(void){return 0;}
static int bkprov_storage_reset_finish(int (*cleanup)(void)) {
 assert(nfc_closed && nfc_error==0);finishes++;return cleanup();
}
static void bkpc_authorization_unbind(void){}
static int bkprov_owner_unbind(void){return 0;}
static int bkprov_network_unbind(void){return 0;}
static void bkprov_identity_clear(int *p){*p=0;}
"""


class ResetNfcTest(unittest.TestCase):
    def run_case(self, body):
        source = (ROOT / "app/bk7258/bk7258_agent_product.c").read_text()
        code = PREFIX + function(source, "product_reset_step")
        code += (
            "\nint main(void){bkpc_tasks_bind(&g_pc_tasks,1,1);"
            + body
            + "\nif(!g_control_bound)assert(g_pc_tasks.binding==0);\nreturn 0;}\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "case.c").write_text(code)
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-function",
                    "-Wno-unused-variable",
                    "-fsanitize=undefined",
                    "-fno-sanitize-recover=all",
                    "-I",
                    str(ROOT / "app/bk7258"),
                    str(ROOT / "app/bk7258/bk7258_pc_tasks.c"),
                    str(path / "case.c"),
                    "-o",
                    str(path / "case"),
                ],
                check=True,
            )
            subprocess.run([str(path / "case")], check=True)

    def test_usb_failed(self):
        self.run_case(
            """usb_error=-EIO;
 assert(product_reset_step()==-EIO);
 assert(usb_stops==1 && !finishes && !clears && !resumes);
 assert(nfc_closed && motion_closed);"""
        )

    def test_usb_before_identity(self):
        self.run_case(
            """assert(product_reset_step()==-EAGAIN);
 assert(usb_stops==1 && usb_closed);pending=0;
 assert(product_reset_step()==1 && usb_closed && !g_identity_bound);"""
        )

    def test_busy(self):
        self.run_case(
            """nfc_error=-EBUSY;
 assert(product_reset_step()==-EBUSY);assert(stops==1 && !finishes && !clears && !resumes);
 nfc_error=0;assert(product_reset_step()==-EAGAIN);assert(finishes==1 && !resumes);
 pending=0;assert(product_reset_step()==1);assert(resumes==1 && owner_opens==1);"""
        )

    def test_failed(self):
        self.run_case(
            """nfc_error=-EIO;
 for(int i=0;i<3;i++)assert(product_reset_step()==-EIO);
 assert(!finishes && !clears && !resumes && nfc_closed);"""
        )

    def test_owner_failure_still_closes_admission(self):
        self.run_case(
            """owner_error=-EIO;
 assert(product_reset_step()==-EIO);assert(stops==1 && nfc_closed && !finishes);"""
        )

    def test_resume_failure(self):
        self.run_case(
            """assert(product_reset_step()==-EAGAIN);pending=0;resume_error=-EIO;
 assert(product_reset_step()==-EIO);assert(!owner_opens && nfc_closed);
 resume_error=0;assert(product_reset_step()==1);assert(owner_opens==1 && finishes==1);"""
        )

    def test_power_intent_and_no_reset(self):
        self.run_case(
            """pending=-EAGAIN;assert(product_reset_step()==0 && !stops);
 pending=0;assert(product_reset_step()==0 && !stops);
 pending=1;g_shutdown_requested=true;assert(product_reset_step()==-EAGAIN);
 pending=0;assert(product_reset_step()==1);assert(!resumes && nfc_closed);"""
        )


class ResetMotionTest(ResetNfcTest):
    # Load this class by exact method name; inherited NFC cases retain their IDs.
    def test_motion_busy(self):
        self.run_case(
            """motion_error=-EBUSY;
 assert(product_reset_step()==-EBUSY);assert(motion_closed && !finishes && !clears);
 motion_error=0;assert(product_reset_step()==-EAGAIN);
 pending=0;assert(product_reset_step()==1);
 assert(motion_resumes==1 && !motion_closed && owner_opens==1);"""
        )

    def test_motion_failed(self):
        self.run_case(
            """motion_error=-EIO;
 for(int i=0;i<3;i++)assert(product_reset_step()==-EIO);
 assert(motion_closed && nfc_closed && !finishes && !clears && !motion_resumes);"""
        )

    def test_motion_other_failure(self):
        self.run_case(
            """owner_error=-EIO;
 assert(product_reset_step()==-EIO);
 assert(motion_closed && motion_stops==1 && nfc_closed && !finishes);"""
        )

    def test_motion_resume_failure(self):
        self.run_case(
            """assert(product_reset_step()==-EAGAIN);
 pending=0;motion_resume_error=-EIO;
 assert(product_reset_step()==-EIO);assert(!owner_opens && motion_closed && nfc_closed);
 motion_resume_error=0;assert(product_reset_step()==1);
 assert(!motion_closed && !nfc_closed && owner_opens==1 && finishes==1);"""
        )

    def test_motion_resume_rollback(self):
        self.run_case(
            """assert(product_reset_step()==-EAGAIN);
 pending=0;resume_error=-EIO;
 assert(product_reset_step()==-EIO);assert(!owner_opens && motion_closed && nfc_closed);
 resume_error=0;assert(product_reset_step()==1);
 assert(!motion_closed && !nfc_closed && finishes==1);"""
        )

    def test_motion_owner_resume_failure(self):
        self.run_case(
            """assert(product_reset_step()==-EAGAIN);
 pending=0;owner_resume_error=-EIO;
 assert(product_reset_step()==-EIO);
 assert(motion_closed && nfc_closed && owner_closed && g_reset_phase==PRODUCT_RESET_FINISHING);
 owner_resume_error=0;assert(product_reset_step()==1);
 assert(!motion_closed && !nfc_closed && finishes==1);"""
        )

    def test_motion_power_intent(self):
        self.run_case(
            """pending=0;assert(product_reset_step()==0 && !motion_stops);
 pending=1;g_shutdown_requested=true;assert(product_reset_step()==-EAGAIN);
 pending=0;assert(product_reset_step()==1);
 assert(motion_closed && motion_stops==1 && !motion_resumes && !owner_opens);"""
        )


if __name__ == "__main__":
    unittest.main()
