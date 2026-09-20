#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build the portable ISO-DEP activation code with a scripted RF transport."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HARNESS = r"""
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <nuttx/contactless/isodep.h>
static uint8_t reply[62];
static size_t reply_length;
static int radio_result, delay_result, releases, exchanges;
static uint32_t slept;
static int exchange(void *p,const uint8_t *tx,size_t n,uint8_t *rx,size_t *len,uint32_t timeout) {
 (void)p;exchanges++;
 assert(n==2&&tx[0]==0xe0&&tx[1]==0x50&&*len==62);
 assert(timeout==4950);
 memcpy(rx,reply,reply_length>62?62:reply_length);*len=reply_length;return radio_result;
}
static int delay(void *p,uint32_t us){(void)p;slept=us;return delay_result;}
static void release(void *p){(void)p;releases++;}
static uint64_t now_ms(void *p){(void)p;return 0;}
static const struct isodep_transport_s ops={exchange,delay,release,now_ms};
static void expect_bad(void) {
 struct isodep_session_s s={0};int before=releases;
 assert(isodep_activate(&s,&ops,0,0x60)==-EPROTO);
 assert(!s.active&&s.transport==0&&s.fsc==0&&releases==before+1);
}
int main(void) {
 struct isodep_session_s s={0};
 reply_length=1;reply[0]=1;
 assert(isodep_activate(&s,&ops,0,0x60)==0);
 assert(s.active&&s.fsc==32&&s.fwt_us==4834&&s.sfgt_us==0);
 assert(isodep_activate(&s,&ops,0,0x60)==-EBUSY);
 isodep_release(&s);int before=releases;isodep_release(&s);assert(releases==before);
 for(int i=0;i<256;i++) {
  reply_length=5;reply[0]=5;reply[1]=0x78;reply[2]=0xff;reply[3]=i;reply[4]=3;slept=0;
  assert(isodep_activate(&s,&ops,0,0x20)==0);
  unsigned fwi=i>>4,sfgi=i&15;if(fwi==15)fwi=4;if(sfgi==15)sfgi=0;
  uint32_t fwt=(((uint64_t)4096<<fwi)*1000000+13559999)/13560000;
  uint32_t sfgt=sfgi?(((uint64_t)4096<<sfgi)*1000000+13559999)/13560000:0;
  assert(s.fwt_us==fwt&&s.sfgt_us==sfgt&&slept==sfgt&&s.fsc==256);
  isodep_release(&s);
 }
 unsigned sizes[]={16,24,32,40,48,64,96,128,256,512,1024,2048,4096,4096,4096,4096};
 for(unsigned i=0;i<16;i++) {
  reply_length=2;reply[0]=2;reply[1]=i;
  assert(isodep_activate(&s,&ops,0,0x20)==0&&s.fsc==sizes[i]);isodep_release(&s);
 }
 for(unsigned flags=0;flags<8;flags++) {
  unsigned required=2+((flags&1)!=0)+((flags&2)!=0)+((flags&4)!=0);
  for(unsigned n=2;n<required;n++) {
   memset(reply,0,sizeof(reply));reply[0]=n;reply[1]=flags<<4;reply_length=n;expect_bad();
  }
 }
 reply_length=0;expect_bad();reply_length=63;expect_bad();
 reply_length=2;reply[0]=1;expect_bad();
 reply_length=62;memset(reply,0xff,sizeof(reply));reply[0]=62;reply[1]=8;
 assert(isodep_activate(&s,&ops,0,0x20)==0);isodep_release(&s);
 radio_result=-ETIMEDOUT;before=releases;
 assert(isodep_activate(&s,&ops,0,0x20)==-ETIMEDOUT&&releases==before+1&&!s.active);
 radio_result=0;delay_result=-EINTR;reply_length=3;reply[0]=3;reply[1]=0x20;reply[2]=0x41;
 assert(isodep_activate(&s,&ops,0,0x20)==-EINTR&&!s.active);delay_result=0;
 before=exchanges;assert(isodep_activate(&s,&ops,0,0x04)==-EPROTONOSUPPORT&&exchanges==before);
 assert(isodep_activate(0,&ops,0,0x20)==-EINVAL);
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="isodep-host-") as tmp:
    root = Path(tmp)
    test = root / "test.c"
    test.write_text(HARNESS)
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=undefined",
            "-I" + str(ROOT / "nuttx/include"),
            str(test),
            str(ROOT / "nuttx/drivers/contactless/isodep.c"),
            "-o",
            str(root / "test"),
        ],
        check=True,
    )
    subprocess.run([str(root / "test")], check=True)
print(
    "PASS: ISO-DEP activation parameters, truncation, RFU defaults and failure release"
)
