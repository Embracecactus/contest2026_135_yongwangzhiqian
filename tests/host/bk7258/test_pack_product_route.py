#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exact PC product adapter with real SDC1/native worker/store/volume.

External authorization snapshot and entropy are controlled peers here; the
separate TLS/PC-guard fixture owns authorization authenticity checks.
"""
from pathlib import Path
import shlex
import subprocess
import tempfile
import argparse
from test_nfc_rf_lifecycle import function, ROOT


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("variant", choices=("session", "revoke"))
    variant = parser.parse_args().variant
    here = ROOT / "tests/host/bk7258"
    product = (ROOT / "app/bk7258/bk7258_agent_product.c").read_text()
    source = (here / "test_display_job_service.c").read_text()
    prefix = r"""
#include "bk7258_pc_usb.h"
#include "bk7258_display_selection_control.h"
/* This fixture verifies the installer route, not default selection. */
static struct bkselection_control_s g_pc_selection;
static uint64_t g_pc_selection_binding,g_pc_selection_grant;
static uint8_t g_pc_selection_client[16];
#define bkselection_control_bind(...) (-ENOTSUP)
#define bkselection_control(...) (-ENOTSUP)
#define bkselection_control_invalidate(s) memset((s),0,sizeof(*(s)))
#define CONFIG_BK7258_DISPLAY_SERVICE 1
#define g_pc_pack wire
static struct bkpc_usb_owner_s g_pc_usb_owner;
static struct bkcontrol_pair_s pair_fixture;
static bool g_identity_bound=true, g_control_bound=true;
static int source_error, random_calls;
static uint64_t grant=8;
#define bkagent_ota_busy() false
#define bkvoice_config_now_ms(ctx) bkdisplay_job_now()
static int random_peer(void *ctx, unsigned char *out, size_t n)
{
  assert(ctx==&pair_fixture.tls.random && n==16); random_calls++;
  memset(out,0,n);out[0]=1;out[1]=2;out[2]=3;out[3]=4;
  return 0;
}
#define mbedtls_ctr_drbg_random random_peer
static int bkpc_authorization_snapshot(void *ctx,uint64_t *binding,
                                       struct bkprov_pc_snapshot_s *view)
{
  (void)ctx;memset(view,0,sizeof(*view));*binding=7;
  view->revision=grant;view->capabilities=BKPC_CAP_RESOURCES;
  view->client[0]=9;view->client[1]=8;view->client[2]=7;view->client[3]=6;
  return source_error;
}
static int product_config(void *ctx,enum bkcontrol_command_e cmd,
                           uint32_t kind,uint32_t off,const uint8_t *r,
                           size_t n,struct bkcontrol_status_s *s)
{(void)ctx;(void)cmd;(void)kind;(void)off;(void)r;(void)n;(void)s;return -ENOTSUP;}
"""
    source = source.replace(
        "static struct bkpack_control_s wire;",
        "static struct bkpack_control_s wire;\n"
        + prefix
        + function(product, "product_pc_pack_step")
        + function(product, "product_pc_config"),
    )
    start = source.index("static int config_route(")
    end = source.index("static int exchange(", start)
    source = (
        source[:start]
        + r"""
static int config_route(void *ctx,enum bkcontrol_command_e cmd,
                        uint32_t kind,uint32_t off,const uint8_t *r,
                        size_t n,struct bkcontrol_status_s *s)
{ return product_pc_config(ctx,cmd,kind,off,r,n,s); }
"""
        + source[end:]
    )
    source = source.replace(
        "assert(bkpack_control_bind(&wire,7,8,client,epoch)==0);",
        r"""
  g_pc_usb_owner.pair=&pair_fixture;
  g_pc_usb_owner.usb.lease.open=true;
  g_pc_usb_owner.usb.lease.binding=7;
  g_pc_usb_owner.usb.lease.revision=8;
  g_pc_usb_owner.usb.lease.capabilities=BKPC_CAP_RESOURCES;
  memcpy(g_pc_usb_owner.usb.lease.client,client,16);
  product_pc_pack_step(true);
  assert(!wire.bound && mounted==0 && created==0);
""",
    )
    source = source.replace(
        'use_session=!strcmp(argv[1],"session");',
        'use_session=!strcmp(argv[1],"session")||!strcmp(argv[1],"revoke");',
    )
    source = source.replace(
        "uint64_t id=status.id;",
        r"""
      uint64_t id=status.id;
      assert(random_calls==1);
      product_pc_pack_step(true);
      assert(wire.bound && wire.id==id);
      if(!strcmp(argv[1],"revoke"))
        {
          grant=9;product_pc_pack_step(true);assert(!wire.bound);
          status=wait_state(-1);assert(pthread_join(worker,NULL)==0);
          assert(status.state==BKDISPLAY_JOB_CANCELED && mounted==unmounted);
          assert(nftw(fixture_root,remove_entry,16,FTW_DEPTH|FTW_PHYS)==0);
          free(bytes);puts("CONTRACT_PASS revoked active production job");return 0;
        }
""",
    )
    source = source.replace(
        "assert(mounted==unmounted);\n  assert(nftw",
        r"""
  /* A grant revision change invalidates the old epoch even without an
   * incoming resource command. No query is needed to enforce revocation. */
  grant=9;product_pc_pack_step(true);
  assert(!wire.bound);
  struct bkcontrol_status_s rejected;
  assert(product_pc_config(NULL,BKCONTROL_CONFIG_READ,16,0,nonce,16,&rejected)==0);
  /* Guard must reject stale lease first. This direct adapter call is not
   * authorization proof; the actual guard is tested through real TLS. */
  assert(mounted==unmounted);
  assert(nftw""",
    )
    # Do not permit a stale direct lease to be re-bound: change it to the
    # freshly reauthenticated grant before exercising a new epoch.
    source = source.replace(
        "struct bkcontrol_status_s rejected;",
        "g_pc_usb_owner.usb.lease.revision=9;\n  struct bkcontrol_status_s rejected;",
    )
    with tempfile.TemporaryDirectory(prefix="pack-route-") as directory:
        temp = Path(directory)
        code = temp / "route.c"
        code.write_text(source)
        result = subprocess.run(
            ["make", "-B", "-n", "build/test_display_job_control"],
            cwd=here,
            check=True,
            text=True,
            capture_output=True,
        )
        command = next(
            line
            for line in result.stdout.replace("\\\n", " ").splitlines()
            if "-DTEST_PACK_CONTROL" in line
        )
        args = shlex.split(command)
        args[args.index("test_display_job_service.c")] = str(code)
        args[args.index("-o") + 1] = str(temp / "route")
        subprocess.run(args, cwd=here, check=True)
        subprocess.run(
            [
                str(temp / "route"),
                variant,
                str(here / "build/shaniu-default-v1.bkep"),
            ],
            cwd=here,
            check=True,
            timeout=15,
        )


if __name__ == "__main__":
    main()
