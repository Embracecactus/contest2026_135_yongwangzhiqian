#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real pair/claim flow with a ready TLS peer and no active network trial."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

# Only the TLS boundary is replaced. Pair, claim and scan declarations and both
# state-machine implementations come directly from the maintained app sources.
TLS_HEADER = r"""
#ifndef TEST_PROVISION_TLS_H
#define TEST_PROVISION_TLS_H
#define __APP_BK7258_PROVISION_TLS_H
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
typedef int mbedtls_x509_crt;
typedef int mbedtls_pk_context;
struct bkprov_tls_s {
 bool initialized, established;
 uint32_t generation;
 uint64_t (*now_ms)(void *);
 void *clock_context;
};
int bkprov_tls_start(struct bkprov_tls_s *, uint32_t, mbedtls_x509_crt *,
                     mbedtls_pk_context *, uint64_t (*)(void *), void *);
void bkprov_tls_close(struct bkprov_tls_s *);
int bkprov_tls_step(struct bkprov_tls_s *);
int bkprov_tls_queue(struct bkprov_tls_s *, const void *, size_t);
ssize_t bkprov_tls_read(struct bkprov_tls_s *, void *, size_t);
#endif
"""

PREFIX = r"""
#include <assert.h>
#include <errno.h>
#include <string.h>
#include "bk7258_provision_pair.h"
#include "bk7258_provision_gatt.h"
static uint64_t clock_ms=100;
static uint32_t generation=7;
static int tls_result=1, queue_error, receipt_result;
static unsigned begins, polls, commits, aborts, receipts, tls_steps, queued;
static uint8_t incoming[64], outgoing[904];
static size_t remaining, cursor;
static uint64_t now(void *p) { (void)p; return clock_ms; }
void mbedtls_platform_zeroize(void *p, size_t n) { memset(p,0,n); }
int mbedtls_ct_memcmp(const void *a, const void *b, size_t n)
{ return memcmp(a,b,n); }
int bkprov_tls_start(struct bkprov_tls_s *tls, uint32_t g,
                     mbedtls_x509_crt *cert, mbedtls_pk_context *key,
                     uint64_t (*clock)(void *), void *context)
{
 (void)cert; (void)key;
 tls->initialized=tls->established=true; tls->generation=g;
 tls->now_ms=clock; tls->clock_context=context; return 0;
}
void bkprov_tls_close(struct bkprov_tls_s *tls) { memset(tls,0,sizeof(*tls)); }
int bkprov_tls_step(struct bkprov_tls_s *tls)
{ assert(tls->initialized); tls_steps++; return tls_result; }
int bkprov_tls_queue(struct bkprov_tls_s *tls, const void *data, size_t n)
{
 assert(tls->established && n<=sizeof(outgoing));
 if(queue_error) return queue_error;
 memcpy(outgoing,data,n); queued++; return 0;
}
ssize_t bkprov_tls_read(struct bkprov_tls_s *tls, void *data, size_t n)
{
 assert(tls->established);
 if(!remaining) return -EAGAIN;
 if(n>remaining) n=remaining;
 memcpy(data,incoming+cursor,n); cursor+=n; remaining-=n; return n;
}
uint32_t bkprov_gatt_generation(void) { return generation; }
int bkprov_scan_start(void) { assert(!"unexpected scan"); return -ENOSYS; }
int bkprov_scan_poll(struct bkprov_scan_result_s *r)
{ (void)r; assert(!"unexpected scan poll"); return -ENOSYS; }
void bkprov_scan_close(void) {}
static int network_begin(void *p, const uint8_t *bundle, size_t n)
{ (void)p; (void)bundle; (void)n; begins++; return 0; }
/* Match the real network backend when this window has no candidate trial. */
static int network_poll(void *p) { (void)p; polls++; return -ENOTCONN; }
static int network_commit(void *p, const uint8_t tx[16],
                           const uint8_t *bundle, size_t n)
{ (void)p; (void)tx; (void)bundle; (void)n; commits++; return 0; }
static void network_abort(void *p) { (void)p; aborts++; }
static const struct bkprov_claim_ops_s network_ops =
{ network_begin, network_poll, network_commit, network_abort };
static int receipt(const uint8_t tx[16])
{
 const uint8_t expected[16]={9};
 assert(!memcmp(tx,expected,sizeof(expected))); receipts++; return receipt_result;
}
static void put32(uint8_t *p, uint32_t n)
{ p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }
static uint32_t get32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void request(uint8_t type, uint32_t seq, const void *data, size_t n)
{
 assert(!remaining && n<=32); memset(incoming,0,sizeof(incoming));
 memcpy(incoming,"SPV1",4); incoming[4]=type; put32(incoming+8,seq);
 incoming[12]=9; put32(incoming+28,n);
 if(n) memcpy(incoming+32,data,n);
 remaining=32+n; cursor=0;
}
static void start(struct bkprov_pair_s *pair)
{
 const uint8_t proof[32]={1};
 assert(bkprov_pair_start(pair,generation,NULL,NULL,proof,true,false,
                          now,NULL,&network_ops,NULL)==0);
 /* Native claim windows install this callback alongside the network ops. */
 pair->receipt=receipt;
 request(1,0,proof,32);
 assert(bkprov_pair_step(pair)==0 && bkprov_pair_step(pair)==0);
 assert(pair->claim.state==BKPROV_LOCAL);
 assert(bkprov_pair_confirm(pair,generation)==0);
 assert(bkprov_pair_step(pair)==0 && pair->claim.state==BKPROV_READY);
}
static void query(struct bkprov_pair_s *pair)
{
 request(5,1,NULL,0);
 assert(bkprov_pair_step(pair)==0);
 assert(pair->query_pending && pair->claim.state==BKPROV_CHECKING);
 assert(!receipts);
}
static void status(struct bkprov_pair_s *pair, enum bkprov_claim_state_e state,
                    int error)
{
 assert(pair->claim.state==state && pair->claim.error==error);
 assert(!memcmp(outgoing,"SPV1",4) && outgoing[4]==128);
 assert(get32(outgoing+8)==1 && outgoing[12]==9 && get32(outgoing+28)==8);
 assert(get32(outgoing+32)==(uint32_t)state);
 assert((int32_t)get32(outgoing+36)==error);
}
"""


class PairReceiptQueryTest(unittest.TestCase):
    def run_case(self, body):
        code = (
            PREFIX
            + "\nint main(void) { struct bkprov_pair_s pair={0}; start(&pair);\n"
            + body
            + "\nbkprov_pair_close(&pair);"
            + "assert(!begins && !polls && !commits && !aborts);return 0;}\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "mbedtls").mkdir()
            (path / "tls.h").write_text(TLS_HEADER)
            (path / "mbedtls/platform_util.h").write_text(
                "#include <stddef.h>\nvoid mbedtls_platform_zeroize(void *, size_t);\n"
            )
            (path / "mbedtls/constant_time.h").write_text(
                "#include <stddef.h>\n"
                "int mbedtls_ct_memcmp(const void *, const void *, size_t);\n"
            )
            (path / "case.c").write_text(code)
            app = ROOT / "app/bk7258"
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
                    "-include",
                    str(path / "tls.h"),
                    "-I",
                    str(path),
                    "-I",
                    str(app),
                    str(path / "case.c"),
                    str(app / "bk7258_provision_claim.c"),
                    str(app / "bk7258_provision_pair.c"),
                    "-o",
                    str(path / "case"),
                ],
                check=True,
            )
            subprocess.run([str(path / "case")], check=True)

    def test_completed_and_absent_receipts(self):
        for result, expected in ((1, "BKPROV_COMMITTED"), (0, "BKPROV_NOT_COMMITTED")):
            with self.subTest(result=result):
                self.run_case(
                    f"receipt_result={result};query(&pair);"
                    "assert(bkprov_pair_step(&pair)==0);"
                    f"status(&pair,{expected},0);"
                    "assert(receipts==1 && !pair.query_pending);"
                    "assert(bkprov_pair_step(&pair)==0 && receipts==1);"
                )

    def test_pending_receipt_retries(self):
        self.run_case(
            """receipt_result=-EAGAIN;query(&pair);
 for(int i=0;i<3;i++) {
  clock_ms++;assert(bkprov_pair_step(&pair)==0);
  status(&pair,BKPROV_CHECKING,0);assert(pair.query_pending);
 }
 assert(receipts==3 && pair.claim.last_ms==clock_ms);
 receipt_result=1;assert(bkprov_pair_step(&pair)==0);
 status(&pair,BKPROV_COMMITTED,0);assert(receipts==4 && !pair.query_pending);"""
        )

    def test_receipt_errors_are_uncertain(self):
        for result, error in (("-EIO", "-EIO"), ("2", "-EIO")):
            with self.subTest(result=result):
                self.run_case(
                    f"receipt_result={result};query(&pair);"
                    "assert(bkprov_pair_step(&pair)==0);"
                    f"status(&pair,BKPROV_UNCERTAIN,{error});"
                    "assert(receipts==1 && !pair.query_pending);"
                )

    def test_pending_receipt_keeps_original_deadline(self):
        self.run_case(
            """receipt_result=-EAGAIN;query(&pair);clock_ms=120099;
 assert(bkprov_pair_step(&pair)==0);status(&pair,BKPROV_CHECKING,0);
 assert(receipts==1);clock_ms=120100;
 assert(bkprov_pair_step(&pair)==0);status(&pair,BKPROV_FAILED,-ETIMEDOUT);
 assert(receipts==1);"""
        )

    def test_query_rejects_clock_rollback(self):
        self.run_case(
            """query(&pair);clock_ms--;
 assert(bkprov_pair_step(&pair)==0);status(&pair,BKPROV_FAILED,-ETIMEDOUT);
 assert(!receipts);"""
        )

    def test_query_rejects_stale_generation(self):
        self.run_case(
            """query(&pair);generation++;
 assert(bkprov_pair_step(&pair)==0);status(&pair,BKPROV_FAILED,-ESTALE);
 assert(!receipts);"""
        )

    def test_query_waits_for_tls_and_preserves_deadline(self):
        self.run_case(
            """query(&pair);tls_result=0;
 assert(bkprov_pair_step(&pair)==0 && !receipts && pair.query_pending);
 clock_ms=120100;assert(bkprov_pair_step(&pair)==0);
 assert(pair.claim.state==BKPROV_FAILED && !receipts);
 tls_result=1;assert(bkprov_pair_step(&pair)==0);
 status(&pair,BKPROV_FAILED,-ETIMEDOUT);assert(!receipts);"""
        )

    def test_tls_failure_closes_query_before_receipt(self):
        self.run_case(
            """query(&pair);tls_result=-EIO;
 assert(bkprov_pair_step(&pair)==-EIO);
 assert(!pair.tls.initialized && pair.claim.state==BKPROV_CLOSED && !receipts);"""
        )

    def test_receipt_is_not_repeated_when_output_is_busy(self):
        self.run_case(
            """receipt_result=1;query(&pair);queue_error=-EAGAIN;
 for(int i=0;i<3;i++)assert(bkprov_pair_step(&pair)==0);
 assert(receipts==1 && pair.report && pair.claim.state==BKPROV_COMMITTED);
 queue_error=0;assert(bkprov_pair_step(&pair)==0);
 status(&pair,BKPROV_COMMITTED,0);assert(receipts==1 && !pair.report);"""
        )


if __name__ == "__main__":
    unittest.main()
