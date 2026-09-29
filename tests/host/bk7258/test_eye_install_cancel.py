#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run the real phone eye-install adapter across its commit boundary.

TLS, HTTP, time, GATT generation and display import are external peers.  The
product_install_eyes body is extracted verbatim from the production product
owner, so the fixture cannot replace the generation/commit decision under
test.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

from test_nfc_rf_lifecycle import ROOT, function


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in (
        "stable",
        "disconnect-cleanup",
        "disconnect-after-commit",
    ):
        raise RuntimeError(
            "selector must be stable, disconnect-cleanup, or disconnect-after-commit"
        )

    product = (ROOT / "app/bk7258/bk7258_agent_product.c").read_text()
    body = function(product, "product_asset_time") + function(
        product, "product_install_eyes"
    )
    source = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

#define LOG_WARNING 4
#define LOG_INFO 6
static void syslog(int level, const char *format, ...)
{ (void)level; (void)format; }

typedef struct { int unused; } mbedtls_x509_crt;
struct bkvoice_wss_tls_ops_s { int unused; };
struct bkcontrol_ota_request_s
{
  char url[256];
  char ca_pem[3073];
  uint8_t ipv4[4];
  uint8_t catalog_sha256[32];
};
struct bkcloud_config_s { uint16_t port; char host[128]; };
struct bkcloud_http_s { size_t received; };
struct bkvoice_tls_s { bool initialized; };
struct bkvoice_tls_config_s
{
  struct in_addr peer_address;
  mbedtls_x509_crt *server_ca;
  int (*trusted_time)(void *);
  uint64_t (*now_ms)(void *);
  bool server_auth_only;
};
struct url_s
{
  char *scheme; size_t schemelen;
  char *host; size_t hostlen;
  uint16_t port;
  char *path; size_t pathlen;
};

static uint32_t generation = 7;
static bool disconnect_cleanup;
static bool disconnect_after_commit;
static unsigned import_calls;
static struct bkvoice_wss_tls_ops_s tls_ops;

static int bkprov_time_get(unsigned int index, uint64_t *utc)
{ (void)index; *utc=1; return 0; }
static uint64_t bkvoice_config_now_ms(void *unused)
{ (void)unused; return 100; }
static uint32_t bkprov_gatt_generation(void) { return generation; }
static void mbedtls_x509_crt_init(mbedtls_x509_crt *crt) { crt->unused=0; }
static void mbedtls_x509_crt_free(mbedtls_x509_crt *crt) { (void)crt; }
static int mbedtls_x509_crt_parse(mbedtls_x509_crt *crt,
                                  const unsigned char *data, size_t size)
{ (void)crt; (void)data; return size ? 0 : -1; }
static int mbedtls_sha256(const unsigned char *data, size_t size,
                          unsigned char output[32], int is224)
{ (void)data; (void)size; (void)is224; memset(output,0xa5,32); return 0; }
static int bkcontrol_eye_request_parse(const uint8_t *record, size_t size,
                                       struct bkcontrol_ota_request_s *out)
{
  (void)record; (void)size; memset(out,0,sizeof(*out));
  strcpy(out->url,"https://eye.invalid/pack.bkep");
  strcpy(out->ca_pem,"CA"); memset(out->catalog_sha256,0xa5,32);
  out->ipv4[0]=127; out->ipv4[3]=1; return 0;
}
static int netlib_parseurl(const char *text, struct url_s *url)
{ (void)text; strcpy(url->host,"eye.invalid"); url->port=443; return 0; }
static int bkvoice_tls_initialize(struct bkvoice_tls_s *tls,
                                  const struct bkvoice_tls_config_s *config)
{ (void)config; tls->initialized=true; return 0; }
static int bkvoice_tls_uninitialize(struct bkvoice_tls_s *tls)
{
  tls->initialized=false;
  /* A real GATT close may race with unrelated cloud-TLS cleanup here. */
  if(disconnect_cleanup) generation++;
  return 0;
}
static const struct bkvoice_wss_tls_ops_s *bkvoice_tls_ops(void)
{ return &tls_ops; }
static int bkcloud_http_get(struct bkcloud_http_s *http,
                            const struct bkcloud_config_s *config,
                            const char *url,
                            const struct bkvoice_wss_tls_ops_s *ops,
                            void *tls, uint64_t deadline, char *data,
                            size_t capacity)
{
  (void)config; (void)url; (void)ops; (void)tls; (void)deadline;
  assert(capacity >= 129); memset(data,0x31,128); http->received=128; return 0;
}
static int bk7258_display_import(const void *data, size_t size)
{
  assert(data && size==128); import_calls++;
  if(disconnect_after_commit) generation++;
  return 0;
}

''' + body + r'''

int main(int argc, char **argv)
{
  uint8_t record[44]={0};
  assert(argc==2);
  disconnect_cleanup=!strcmp(argv[1],"disconnect-cleanup");
  disconnect_after_commit=!strcmp(argv[1],"disconnect-after-commit");
  int ret=product_install_eyes(record,sizeof(record));
  if(disconnect_cleanup)
    {
      assert(ret==-ECANCELED);
      assert(import_calls==0);
      puts("CONTRACT_PASS eye install disconnect before commit");
    }
  else if(disconnect_after_commit)
    {
      assert(ret==0);
      assert(import_calls==1);
      puts("CONTRACT_PASS eye install disconnect after commit");
    }
  else
    {
      assert(ret==0);
      assert(import_calls==1);
      puts("CONTRACT_PASS eye install stable commit");
    }
  return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="eye-install-cancel-") as directory:
        temp = Path(directory)
        code = temp / "eye_install_cancel.c"
        binary = temp / "eye_install_cancel"
        code.write_text(source)
        subprocess.run(
            [
                "cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O2",
                str(code), "-o", str(binary),
            ],
            check=True,
        )
        return subprocess.run([str(binary), sys.argv[1]], timeout=10).returncode


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print("SETUP_ERROR:", error, file=sys.stderr)
        raise SystemExit(2)
